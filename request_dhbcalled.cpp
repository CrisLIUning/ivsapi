#include "./request_dhbcalled.h"
#include <log4cplus/loggingmacros.h>

namespace chilli {
	namespace http {

		request_dhbcalled::request_dhbcalled(HttpRequest::http_method method, const std::string& uri, std::map<std::string, std::string> headers, ivsapi_session* session)
			:HttpRequest(session->getId(), method, uri, "", 0, headers), m_pSession(session)
		{
			log = log4cplus::Logger::getInstance("request_dhbcalled");
		}


		request_dhbcalled::~request_dhbcalled()
		{
			LOG4CPLUS_TRACE(log, " request_dhbcalled deconstructor");
		}

		void request_dhbcalled::onResponse()
		{
			std::string response = this->GetResponse();
			LOG4CPLUS_DEBUG(log, m_pSession->getId() << " Response:" << response);
			if (this->GetStatus() == HTTP_OK) {
				LOG4CPLUS_DEBUG(log, m_pSession->getId() << " GetStatus:HTTP_OK");

			}
			else if (this->GetStatus() == 401) {
				LOG4CPLUS_ERROR(log, m_pSession->getId() << " http 401 status ");
			}

			m_pSession->callback_dhbcalled(this->GetStatus(), response);
		}

	}
}