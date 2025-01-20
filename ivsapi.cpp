#include "ivsapi.h"
#include "ivsapi_session.h"
#include <log4cplus/loggingmacros.h>
#include "../tinyxml2/tinyxml2.h"
#include "../utils/json/writer_helper.h"
#include "../uuid.h"
#include "../http/md5.h"
#include <apr_hash.h>
#include "RequestIVSAPI_DHB.h"
#include "RequestIVSAPI_BlackList1.h"
#include "RequestIVSAPI_BlackList2.h"

namespace chilli {
	namespace http {

		ivsapi::ivsapi(const std::string& id, uint32_t threadSize) :ProcessModule(id, threadSize), http_server(id)
		{
			log = log4cplus::Logger::getInstance("chilli.http.ivsapi");
			LOG4CPLUS_TRACE(log, this->getId() << "construct");
		}

		ivsapi::~ivsapi()
		{
			LOG4CPLUS_TRACE(log, this->getId() << " deconstructor");
		}

		int ivsapi::Start()
		{
			LOG4CPLUS_TRACE(log, this->getId() << " Start");
			ProcessModule::Start();
			http_server::Start();		
			return 0;
		}

		int ivsapi::Stop()
		{
			LOG4CPLUS_TRACE(log, this->getId() << " Stop");
			ProcessModule::Stop();
			http_server::Stop();
			return 0;
		}

		bool ivsapi::LoadConfig(const std::string& configContext)
		{
			using namespace tinyxml2;
			tinyxml2::XMLDocument config;
			if (config.Parse(configContext.c_str()) != XMLError::XML_SUCCESS) {
				LOG4CPLUS_ERROR(log, this->getId() << " load config error:" << config.ErrorName() << ":" << config.GetErrorStr1());
				return false;
			}
			XMLElement* agent_httpserver = config.FirstChildElement("IVSAPI");
			const char* host = agent_httpserver->Attribute("Host");
			const char* port = agent_httpserver->Attribute("Port");
			this->m_Host = host ? host : std::string();
			if (port) {
				try {
					m_Port = std::stoul(port);
				}
				catch (...)
				{
				}
			}

			XMLElement* url = agent_httpserver->FirstChildElement("UrlInterface");
			if (url)
			{
				m_url_interface = url->GetText();
			}

			XMLElement* apikey = agent_httpserver->FirstChildElement("ApiKey");
			if (apikey)
			{
				m_apikey = apikey->GetText();
			}			

			return true;
		}

		void ivsapi::run()
		{
			LOG4CPLUS_TRACE(log, this->getId() << " run begin ");

			for (auto& m : model::ProcessModule::g_Modules) {
				if (m->getId().find("mysql") != std::string::npos) {
					m_mysqlmodule = dynamic_cast<DataBase::MySqlModule*>(m.get());
					break;
				}
				if (m->getId().find("freeswitch001") != std::string::npos) {
					m_fsModule = dynamic_cast<FreeSwitch::FreeSwitchModule*>(m.get());
					break;
				}
			}

			while (m_bRunning)
			{
				try
				{
					model::EventType_t Event;
					if (m_RecEvtBuffer.Get(Event, 1 * 1000) && !Event->typeName.empty())
					{
						const Json::Value& jsonEvent = Event->jsonEvent;
						const std::string& sessionId = Event->id;

						apr_ssize_t klen = sessionId.length();
						uint32_t hash = apr_hashfunc_default(sessionId.c_str(), &klen);
						hash %= m_executeThread.size();
						m_executeThread[hash].eventQueue.Put(Event);

					}

				}
				catch (std::exception& e)
				{
					LOG4CPLUS_ERROR(log, this->getId() << " run error:" << e.what());
				}
			}

			LOG4CPLUS_TRACE(log, this->getId() << " run stop ");
		}

		void ivsapi::execute(TexecuteThread* threadData)
		{
			LOG4CPLUS_INFO(log, this->getId() << " Process thread Starting...");

			while (m_bRunning)
			{
				while (m_bRunning)
				{
					model::EventType_t Event;
					if (threadData->eventQueue.Get(Event, 1 * 1000) && !Event->jsonEvent["param"].empty()) {

						threadData->lastProcesTime = std::chrono::system_clock::now();
						Json::Value& jsonEvent = Event->jsonEvent;
						LOG4CPLUS_TRACE(log, Event->id << " execute jsonEvent:" << Json::toString(Event->jsonEvent));

						if (jsonEvent["event"] == "RequestIVSAPI_DHB")
							call_dhb(jsonEvent["param"]);
						else if (jsonEvent["event"] == "RequestIVSAPI_BLACKLIST1")
							call_blacklist1(jsonEvent["param"]);
						else if (jsonEvent["event"] == "RequestIVSAPI_BLACKLIST2")
							call_blacklist2(jsonEvent["param"]);
						
					}
				}
			}

			LOG4CPLUS_INFO(log, this->getId() << " Process thread Stoped.");
			log4cplus::threadCleanup();
		}

		void ivsapi::fireSend(const fsm::FireDataType& fireData, const void* param)
		{
			LOG4CPLUS_TRACE(log, fireData.from << " fireSend:" << fireData.event);

			Json::Value newEvent;
			newEvent["from"] = fireData.from;
			newEvent["event"] = fireData.event;
			newEvent["type"] = fireData.type;
			newEvent["param"] = fireData.param;
			newEvent["id"] = fireData.from;

			this->PushEvent(model::EventType_t(new model::_EventType(newEvent)));
			/*if (fireData.event == "RequestIVSAPI_DHB")
				call_dhb(fireData.param);
			else if (fireData.event == "RequestIVSAPI_BLACKLIST1")
				call_blacklist1(fireData.param);
			else if (fireData.event == "RequestIVSAPI_BLACKLIST2")
				call_blacklist2(fireData.param);*/
		}

		const log4cplus::Logger& ivsapi::get_logger_http_server()
		{
			return ProcessModule::getLogger();
		}

		void ivsapi::http_callback_handle_generic(struct evhttp_request* req)
		{
			try
			{
				const char* uri = evhttp_request_get_uri(req);//获取请求uri
				LOG4CPLUS_DEBUG(log, this->getId() << " http_callback_handle_generic begin uri:" << uri);
				
				std::string str_uri = uri;
				if (str_uri != m_url_interface) {
					write_response_fail(req, "访问地址错误");
					return;
				}


				std::string error_msg = "POST参数错误";
				struct evbuffer* buf = evhttp_request_get_input_buffer(req);
				std::string strBuf;
				while (evbuffer_get_length(buf)) {
					int n;
					char cbuf[256];
					n = evbuffer_remove(buf, cbuf, sizeof(cbuf));
					if (n > 0)
						strBuf.append(cbuf, n);
				}

				LOG4CPLUS_TRACE(log, this->getId() << "recv msg,recv=" << strBuf);
				if (strBuf.empty()) {
					write_response_fail(req, error_msg);
					return;
				}

				Json::Value jsonResult;
				if (!parse_string_to_json(strBuf, jsonResult)) {
					write_response_fail(req, error_msg);
					return;
				}

				if (!jsonResult.isMember("token") && !jsonResult.isMember("data")) {
					write_response_fail(req, error_msg);
					return;
				}
				else {

					if (jsonResult["data"]["ts"].isNull()) {
						write_response_fail(req, error_msg);
						return;
					}

					long currentTimeStamp = (long)helper::time::getTimeStamp();
					long requestTs = stol(jsonResult["data"]["ts"].asString());
					if (abs(currentTimeStamp - requestTs) < 180000) {
						//时间戳允许误差3分钟
					}
					else
					{
						write_response_fail(req, "时间戳错误");
						LOG4CPLUS_TRACE(log, this->getId() << " currentTimeStamp=" << currentTimeStamp << ",requestTs=" << requestTs);
						return;
					}

					std::string client_token = jsonResult["token"].asString();
					std::string md5_token = md5(m_apikey + jsonResult["data"]["method"].asString() + jsonResult["data"]["ts"].asString());
					if (md5_token != client_token) {
						LOG4CPLUS_TRACE(log, this->getId() << " md5_token=" << md5_token);
						write_response_fail(req, "秘钥验证错误");
						return;
					}

					ivsapi_session request_session(this, helper::uuid(), jsonResult["data"], req);

				}
			}
			catch (const std::exception& e)
			{
				LOG4CPLUS_ERROR(log, this->getId() << " http_callback_handle_generic error:" << e.what());
			}

		}

		void ivsapi::write_response(struct evhttp_request* req, const uint32_t& code, const std::string& messge, const Json::Value& data)
		{
			evhttp_add_header(req->output_headers, "Content-Type", "application/json; charset=UTF-8");
			evhttp_add_header(req->output_headers, "Connection", "close");
			evhttp_add_header(evhttp_request_get_output_headers(req), "Access-Control-Allow-Origin", "*");
			evhttp_add_header(evhttp_request_get_output_headers(req), "Access-Control-Allow-Headers", "Authorization,Origin, X-Requested-With, Content-Type, Accept");
			evhttp_add_header(evhttp_request_get_output_headers(req), "Access-Control-Allow-Methods", "GET,POST");

			Json::Value json_buf;
			json_buf["code"] = code;
			json_buf["messge"] = messge;
			json_buf["data"] = data;
			Json::FastWriter writer;
			std::string strBuf = writer.write(json_buf);

			struct evbuffer* buf = evbuffer_new();
			evbuffer_add_printf(buf, "%s", strBuf.c_str());
			evhttp_send_reply(req, HTTP_OK, "OK", buf);
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi::write_response evhttp_send_reply code=" << code << ",req=" << req);
		}

		void ivsapi::write_response_success(struct evhttp_request* req, const Json::Value& data)
		{
			write_response(req, 0, "操作成功", data);
		}

		void ivsapi::write_response_fail(struct evhttp_request* req, std::string messge)
		{
			write_response(req, 9, messge, "");
		}

		void ivsapi::write_response_fail2(struct evhttp_request* req, const Json::Value& data, std::string messge)
		{
			write_response(req, 9, messge, data);
		}

		bool ivsapi::parse_string_to_json(const std::string& str, Json::Value& json)
		{
			//LOG4CPLUS_TRACE(log, this->getId() << " parse_string_to_json begin:" << str);
			try
			{
				Json::Reader reader;
				if (!reader.parse(str, json)) {
					LOG4CPLUS_TRACE(log, this->getId() << " parse_string_to_json fail,str:" << str);
					return false;
				}

				return true;
			}
			catch (const std::exception& e)
			{
				LOG4CPLUS_TRACE(log, this->getId() << " parse_string_to_json error:" << e.what());
				return false;
			}

		}

		void ivsapi::call_dhb(const Json::Value& param) {
			string params = "called_number=" + param["call_number"].asString() + "&hash_type=1&mch_id=" + param["api_secret"].asString() + "&apikey=" + param["api_key"].asString();
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi::call_dhb params:" << params);

			string md5_string = md5(params);
			string sign = helper::string::toUpper(md5_string);
			string url = param["api_url"].asString() + "?" + params + "&sign=" + sign;

			std::map<std::string, std::string> headers = {};
			std::string sessionId = param["id"].asString();
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi::call_dhb sessionId:" << sessionId);
			RequestIVSAPI_DHB requestIVSAPI(HttpRequest::http_method::HTTP_REQ_POST, url, headers, this, sessionId);
			requestIVSAPI.Send();
		}

		void ivsapi::callback_dhb(const uint32_t httpstatus, const std::string& response, const std::string& sessionId) {
			LOG4CPLUS_TRACE(log, this->getId() << " callback_dhb httpstatus:" << httpstatus);
			LOG4CPLUS_TRACE(log, this->getId() << " callback_dhb response:" << response);

			bool isValid = false;
			Json::Value jsonResult;
			if (httpstatus == 200) {
				if (parse_string_to_json(response, jsonResult)) {
					if (jsonResult["status"].asInt() == 0) {
						if (!jsonResult["data"]["is_risk"].asBool()) {
							//验证通过
							isValid = true;
						}
					}
					else
						LOG4CPLUS_ERROR(log, this->getId() << " callback_dhb return status not 0");
				}
			}
			else if (httpstatus == 455) {
				parse_string_to_json(response, jsonResult);
				LOG4CPLUS_TRACE(log, this->getId() << " callback_dhb httpstatus 455 response");
			}
			else
				parse_string_to_json(response, jsonResult);


			Json::Value newEvt;
			newEvt["id"] = sessionId;
			newEvt["type"] = "event";
			newEvt["param"] = jsonResult;
			newEvt["event"] = isValid?"RequestIVSAPI_DHB_SUCCESSFUL": "RequestIVSAPI_DHB_FAILED";
			model::EventType_t evt(new model::_EventType(newEvt));
			m_fsModule->PushEvent(evt);
		}

		void ivsapi::call_blacklist1(const Json::Value& param) {
			string url = param["api_url"].asString();
			std::map<std::string, std::string> headers = {};
			std::string sessionId = param["id"].asString();
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi::call_blacklist1 sessionId:" << sessionId);

			Json::StreamWriterBuilder writer;
			std::string jsonString = Json::writeString(writer, param);
			RequestIVSAPI_BlackList1 requestIVSAPI(HttpRequest::http_method::HTTP_REQ_POST, url, headers, this, sessionId, jsonString);
			requestIVSAPI.Send();
		}

		void ivsapi::callback_blacklist1(const uint32_t httpstatus, const std::string& response, const std::string& sessionId) {
			LOG4CPLUS_TRACE(log, this->getId() << " callback_blacklist1 httpstatus:" << httpstatus);
			LOG4CPLUS_TRACE(log, this->getId() << " callback_blacklist1 response:" << response);

			bool isValid = false;
			Json::Value jsonResult;
			int status = 1;
			if (httpstatus == 200) {
				status = 0;
				if (parse_string_to_json(response, jsonResult)) {
					if (!jsonResult.isMember("forbid")) {
						//验证通过
						isValid = true;
					}
					else
						LOG4CPLUS_ERROR(log, this->getId() << " callback_blacklist1 风险号码");
				}
			}
			else if (httpstatus == 455) {
				parse_string_to_json(response, jsonResult);
				LOG4CPLUS_TRACE(log, this->getId() << " callback_blacklist1 httpstatus 455 response");
			}
			else
				parse_string_to_json(response, jsonResult);

			jsonResult["status"] = status;
			Json::Value newEvt;
			newEvt["id"] = sessionId;
			newEvt["type"] = "event";
			newEvt["param"] = jsonResult;
			newEvt["event"] = isValid ? "RequestIVSAPI_BLACKLIST1_SUCCESSFUL" : "RequestIVSAPI_BLACKLIST1_FAILED";
			model::EventType_t evt(new model::_EventType(newEvt));
			m_fsModule->PushEvent(evt);
		}

		void ivsapi::call_blacklist2(const Json::Value& param) {
			
			string ts = std::to_string((long)helper::time::getTimeStamp()).substr(0, 10);
			string appid = param["appid"].asString();
			string api_secret = param["api_secret"].asString();
			Json::Value data;
			data["appid"] = appid;
			data["sign"] = md5(appid + ts + api_secret);
			data["ts"] = std::stoi(ts);
			data["phones"] = param["phones"];
			data["chkPlanCode"] = param["chkPlanCode"];
			Json::StreamWriterBuilder writer;
			std::string jsonString = Json::writeString(writer, data);

			std::map<std::string, std::string> headers = {};
			RequestIVSAPI_BlackList2 requestIVSAPI(HttpRequest::http_method::HTTP_REQ_POST, param["api_url"].asString(), headers, this, param["id"].asString(), jsonString);
			requestIVSAPI.Send();
		}

		void ivsapi::callback_blacklist2(const uint32_t httpstatus, const std::string& response, const std::string& sessionId) {
			LOG4CPLUS_TRACE(log, sessionId << " callback_blacklist2 response:" << response);

			bool isValid = false;
			Json::Value jsonResult;
			int status = 1;
			if (httpstatus == 200) {
				status = 0;
				if (parse_string_to_json(response, jsonResult)) {
					if (jsonResult["code"].asInt() == 200) {
						if (jsonResult["dtos"].size() > 0 && jsonResult["dtos"][0]["act"].asInt() == 0) {
							//验证通过
							isValid = true;
						}
					}
					else
						LOG4CPLUS_ERROR(log, sessionId << " callback_blacklist2 失败:" << jsonResult["msg"].asString());
				}
			}
			else{
				parse_string_to_json(response, jsonResult);
				LOG4CPLUS_TRACE(log, sessionId << " callback_blacklist2 callback_blacklist2 失败");
			}

			jsonResult["status"] = status;
			Json::Value newEvt;
			newEvt["id"] = sessionId;
			newEvt["type"] = "event";
			newEvt["param"] = jsonResult;
			newEvt["event"] = isValid ? "RequestIVSAPI_BLACKLIST2_SUCCESSFUL" : "RequestIVSAPI_BLACKLIST2_FAILED";
			model::EventType_t evt(new model::_EventType(newEvt));
			m_fsModule->PushEvent(evt);
		}

	}
}