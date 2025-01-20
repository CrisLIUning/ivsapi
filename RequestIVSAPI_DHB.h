#pragma once
#include "./HttpRequest.h"
#include "./ivsapi.h"

namespace chilli {
	namespace http {

		class RequestIVSAPI_DHB : public HttpRequest
		{
		public:
			RequestIVSAPI_DHB(HttpRequest::http_method method, const std::string& uri, std::map<std::string, std::string> headers, ivsapi* ivsapiModule,const std::string& sessionId);
			~RequestIVSAPI_DHB();

			virtual void onResponse() override;

		private:
			log4cplus::Logger log;
			ivsapi* m_Module = nullptr;
			std::string m_sessionId;
		};

	}
};
