#include "RequestIVSAPI_DHB.h"
#include <log4cplus/loggingmacros.h>

namespace chilli {
	namespace http {

		RequestIVSAPI_DHB::RequestIVSAPI_DHB(HttpRequest::http_method method, const std::string& uri, std::map<std::string, std::string> headers, ivsapi* ivsapiModule,const std::string& sessionId)
			:HttpRequest(ivsapiModule->getId(), method, uri, "", 0, headers), m_Module(ivsapiModule),m_sessionId(sessionId)
		{
			log = log4cplus::Logger::getInstance("RequestIVSAPI_DHB");
			LOG4CPLUS_TRACE(log, " RequestIVSAPI_DHB constructor");
		}


		RequestIVSAPI_DHB::~RequestIVSAPI_DHB()
		{
			LOG4CPLUS_TRACE(log, " RequestIVSAPI_DHB deconstructor");
		}

		void RequestIVSAPI_DHB::onResponse()
		{
			std::string response = this->GetResponse();
			LOG4CPLUS_DEBUG(log, m_Module->getId() << " Response:" << response);
			if (this->GetStatus() == HTTP_OK) {
				LOG4CPLUS_DEBUG(log, m_Module->getId() << " GetStatus:HTTP_OK");

			}
			else if (this->GetStatus() == 401) {
				LOG4CPLUS_ERROR(log, m_Module->getId() << " http 401 status ");
			}

			m_Module->callback_dhb(this->GetStatus(), response,m_sessionId);
		}

	}
}