#pragma once
#include "./HttpRequest.h"
#include "./ivsapi_session.h"

namespace chilli {
	namespace http {

		class request_dhbcalled : public HttpRequest
		{
		public:
			request_dhbcalled(HttpRequest::http_method method, const std::string& uri, std::map<std::string, std::string> headers, ivsapi_session* session);
			~request_dhbcalled();

			virtual void onResponse() override;

		private:
			log4cplus::Logger log;
			ivsapi_session* m_pSession = nullptr;
		};

	}
}