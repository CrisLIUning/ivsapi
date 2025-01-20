#pragma once
#include "event2/event-config.h"

#include <event2/event.h>
#include <event2/http.h>
#include <event2/http_struct.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <string>
#include <log4cplus/logger.h>
#include <log4cplus/loggingmacros.h>
#include <map>
#include <mutex>
#include <atomic>
#include <sstream>
#include <cstring>


class HttpRequest {
public:
	typedef std::map<std::string, std::string> HeaderType;
	enum http_method {
		HTTP_REQ_GET = 1 << 0,
		HTTP_REQ_POST = 1 << 1,
		HTTP_REQ_HEAD = 1 << 2,
		HTTP_REQ_PUT = 1 << 3,
		HTTP_REQ_DELETE = 1 << 4,
		HTTP_REQ_OPTIONS = 1 << 5,
		HTTP_REQ_TRACE = 1 << 6,
		HTTP_REQ_CONNECT = 1 << 7,
		HTTP_REQ_PATCH = 1 << 8
	};
public:
	HttpRequest(const std::string& sessionId, http_method method, const std::string& uri, const std::string& data = "", int timeout = 0, std::map<std::string, std::string> headers = {}) :m_status(0), m_method(method), m_uri(uri)
		, m_data(data), m_timeout(timeout), evcon(NULL), http_uri(NULL), base(NULL), m_sessionId(sessionId), m_requestTimes(0), m_header(headers)
	{
		//m_sessionId.append(".").append(make_session());
		log = log4cplus::Logger::getInstance("HttpRequest");
		LOG4CPLUS_TRACE(log, m_sessionId << " " << this << " construct");
	}
	virtual ~HttpRequest() {
		if (evcon)
			evhttp_connection_free(evcon);
		if (http_uri)
			evhttp_uri_free(http_uri);
		if (base) {
			event_base_free(base);
			base = NULL;
		}
		LOG4CPLUS_TRACE(log, m_sessionId << " " << this << " deconstrctor");
	}

	const std::string& GetResponse() { return m_responseBody; }
	const std::string& GetUri() { return m_uri; }
	int GetTimeOut() { return m_timeout; }
	const std::string& GetData() { return m_data; }
	http_method GetMethod() { return m_method; }

	int GetStatus() { return m_status; }

	void AddHeader(const std::string& key, const std::string& value)
	{
		m_header[key] = value;
	}

	const HeaderType& GetHeader() { return m_header; }

	virtual void onResponse() {}

	int Send(struct event_base* _base = NULL) {

		struct evhttp_request* req = NULL;
		struct evkeyvalq* output_headers = NULL;
		std::string uri;
		int r, port;
		HeaderType::const_iterator it;
		const char* path, * query, * host;

		m_requestTimes++;
		m_status = 0;

		if (http_uri)
			evhttp_uri_free(http_uri);
		http_uri = evhttp_uri_parse(m_uri.c_str());
		if (http_uri == NULL) {
			LOG4CPLUS_ERROR(log, m_sessionId << " " << m_uri << " parse failed");
			m_status = -1;
			goto cleanup;
		}

		host = evhttp_uri_get_host(http_uri);
		if (host == NULL) {
			LOG4CPLUS_ERROR(log, m_sessionId << " " << m_uri << " uri must have a host");
			m_status = -2;
			goto cleanup;
		}

		port = evhttp_uri_get_port(http_uri);
		if (port == -1) {
			port = 80;
		}

		path = evhttp_uri_get_path(http_uri);
		if (strlen(path) == 0) {
			path = "/";
		}

		query = evhttp_uri_get_query(http_uri);
		if (query == NULL) {
			uri = path;
		}

		else {
			uri = path;
			uri.append("?").append(query);
		}

		if (_base == NULL) {
			if (base) {
				event_base_free(base);
			}
			base = event_base_new();
			if (!base) {
				LOG4CPLUS_ERROR(log, m_sessionId << " " << "event_base_new()");
				m_status = -3;
				goto cleanup;
			}
			_base = base;
		}

		if (evcon)
			evhttp_connection_free(evcon);
		evcon = evhttp_connection_base_new(_base, NULL,
			host, port);
		if (evcon == NULL) {
			LOG4CPLUS_ERROR(log, m_sessionId << " " << "evhttp_connection_base_new() failed");
			m_status = -4;
			goto cleanup;
		}

		if (m_timeout > 0) {
			evhttp_connection_set_timeout(evcon, m_timeout);
			LOG4CPLUS_TRACE(log, m_sessionId << " " << "set timeout:" << m_timeout);
		}

		evhttp_connection_set_closecb(evcon, http_on_close, this);

		// Fire off the request
		req = evhttp_request_new(http_request_done, this);
		if (req == NULL) {
			LOG4CPLUS_ERROR(log, m_sessionId << " " << "evhttp_request_new() failed");
			m_status = -5;
			goto cleanup;
		}

		output_headers = evhttp_request_get_output_headers(req);
		evhttp_add_header(output_headers, "Host", host);
		evhttp_add_header(output_headers, "Connection", "close");
		//evhttp_add_header(output_headers, "Content-Type", "application/json;charset=utf-8");		

		for (it = m_header.begin(); it != m_header.end(); ++it) {
			evhttp_add_header(output_headers, it->first.c_str(), it->second.c_str());
			LOG4CPLUS_TRACE(log, m_sessionId << " " << "add header:" << it->first.c_str() << "," << it->second.c_str());
		}

		if(m_header.empty()){
			evhttp_add_header(output_headers, "Content-Type", "application/json;charset=utf-8");
			LOG4CPLUS_TRACE(log, m_sessionId << " " << "add header:Content-Type,application/json;charset=utf-8");
		}

		if (!m_data.empty()) {
			struct evbuffer* output_buffer = evhttp_request_get_output_buffer(req);
			evbuffer_add(output_buffer, m_data.c_str(), m_data.length());
			LOG4CPLUS_DEBUG(log, m_sessionId << " " << "add http body:" << m_data);
		}

		LOG4CPLUS_DEBUG(log, m_sessionId << " " << "request starting..." << m_uri);
		r = evhttp_make_request(evcon, req, (evhttp_cmd_type)m_method, uri.c_str());
		if (r != 0) {
			LOG4CPLUS_ERROR(log, m_sessionId << " " << "evhttp_make_request() failed,r:" << r);
			m_status = -6;
			goto cleanup;
		}

		if (this->base) {
			event_base_dispatch(this->base);
		}
	cleanup:
		//if (req)
		//	evhttp_request_free(req);

		return m_status;
	}

public:
	std::string m_uri;
private:
	int m_status;
	http_method m_method;
	//std::string m_uri;
	std::string m_responseBody;
	std::map<std::string, std::string>m_header;
	std::string m_data;
	int m_timeout;
	struct evhttp_connection* evcon;
	struct evhttp_uri* http_uri;
	struct event_base* base;
protected:
	std::string m_sessionId;
	uint32_t m_requestTimes;
	log4cplus::Logger log;
private:
	HttpRequest(const HttpRequest&) = delete;
	HttpRequest& operator=(const HttpRequest&) = delete;

private:
	/************************** Request Function ******************************/
	static void http_request_done(struct evhttp_request* req, void* arg)
	{
		HttpRequest* This = (HttpRequest*)arg;
		if (req == NULL) {
			LOG4CPLUS_WARN(This->log, This->m_sessionId << " " << " connect error");
			goto done;
		}
		This->m_status = req->response_code;

		switch (req->response_code)
		{
		case HTTP_OK:
		{
			LOG4CPLUS_TRACE(This->log, This->m_sessionId << " " << "200 OK");
			struct evbuffer* buf = evhttp_request_get_input_buffer(req);
			size_t len = evbuffer_get_length(buf);
			std::vector<char>databuf(len);
			evbuffer_remove(buf, databuf.data(), len);
			This->m_responseBody = std::string(databuf.begin(), databuf.end());
			if (This->GetMethod() != HTTP_REQ_GET)
				LOG4CPLUS_DEBUG(This->log, This->m_sessionId << " " << "response " << This->m_responseBody);
			break;
		}
		case HTTP_MOVEPERM:
			LOG4CPLUS_WARN(This->log, This->m_sessionId << " " << "the uri moved permanently");
			break;
		case HTTP_MOVETEMP:
		{
			LOG4CPLUS_WARN(This->log, This->m_sessionId << " " << "the uri moved temp");
			break;
		}
		case 455:
		{
			LOG4CPLUS_WARN(This->log, This->m_sessionId << " " << "the http status 455");
			struct evbuffer* buf = evhttp_request_get_input_buffer(req);
			size_t len = evbuffer_get_length(buf);
			std::vector<char>databuf(len);
			evbuffer_remove(buf, databuf.data(), len);
			This->m_responseBody = std::string(databuf.begin(), databuf.end());
			if (This->GetMethod() != HTTP_REQ_GET)
				LOG4CPLUS_DEBUG(This->log, This->m_sessionId << " " << "response " << This->m_responseBody);

			break;
		}

		default:
			LOG4CPLUS_WARN(This->log, This->m_sessionId << " " << req->response_code);
			struct evbuffer* buf = evhttp_request_get_input_buffer(req);
			size_t len = evbuffer_get_length(buf);
			std::vector<char>databuf(len);
			evbuffer_remove(buf, databuf.data(), len);
			This->m_responseBody = std::string(databuf.begin(), databuf.end());
			if (This->GetMethod() != HTTP_REQ_GET)
				LOG4CPLUS_DEBUG(This->log, This->m_sessionId << " " << "response " << This->m_responseBody);
			break;
		}
	done:
		if (This->base) {
			event_base_loopbreak(This->base);
		}

		This->onResponse();

	}
	static void http_on_close(struct evhttp_connection* evcon, void* arg)
	{
		HttpRequest* This = (HttpRequest*)arg;
		LOG4CPLUS_TRACE(This->log, This->m_sessionId << " " << "closed");
	}

	static const std::string make_session()
	{
		static std::atomic<uint64_t> sessinId;
		std::stringstream stream;
		stream << "HR" << ++sessinId;
		return stream.str();
	}
};
typedef HttpRequest* HttpRequestPtr;