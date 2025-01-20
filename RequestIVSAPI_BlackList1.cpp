#include "RequestIVSAPI_BlackList1.h"
#include <log4cplus/loggingmacros.h>

namespace chilli {
	namespace http {

		RequestIVSAPI_BlackList1::RequestIVSAPI_BlackList1(HttpRequest::http_method method, const std::string& uri, std::map<std::string, std::string> headers, ivsapi* ivsapiModule, const std::string& sessionId, const std::string& data)
			:HttpRequest(ivsapiModule->getId(), method, uri, data, 0, headers), m_Module(ivsapiModule), m_sessionId(sessionId)
		{
			log = log4cplus::Logger::getInstance("RequestIVSAPI_BlackList1");
			LOG4CPLUS_TRACE(log, " RequestIVSAPI_BlackList1 constructor");
		}


		RequestIVSAPI_BlackList1::~RequestIVSAPI_BlackList1()
		{
			LOG4CPLUS_TRACE(log, " RequestIVSAPI_BlackList1 deconstructor");
		}

		void RequestIVSAPI_BlackList1::onResponse()
		{
			std::string response = this->GetResponse();
			LOG4CPLUS_DEBUG(log, m_Module->getId() << " Response:" << response);
			if (this->GetStatus() == HTTP_OK) {
				LOG4CPLUS_DEBUG(log, m_Module->getId() << " GetStatus:HTTP_OK");

			}
			else if (this->GetStatus() == 401) {
				LOG4CPLUS_ERROR(log, m_Module->getId() << " http 401 status ");
			}

			m_Module->callback_blacklist1(this->GetStatus(), response, m_sessionId);
		}

	}
}