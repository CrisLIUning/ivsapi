#pragma once
#include <json/config.h>
#include <json/json.h>
#include <log4cplus/loggingmacros.h>
#include "ivsapi.h"
#include "../FreeSWITCH/FreeSwitchModule.h"
#include "../http/md5.h"
#include "../stringHelper.h"

namespace chilli {
	namespace http {

		class request_dbhcaller;
		class request_dbhcalled;
		class ivsapi_session
		{
		public:
			ivsapi_session(ivsapi *owner, const std::string &session_id, const Json::Value &json_data, struct evhttp_request *req);
			~ivsapi_session();
			const std::string getId();
			const log4cplus::Logger getLogger_agent_httpserver();
			void call_dhbcaller(const std::string& call_number);
			void callback_dhbcaller(const uint32_t httpstatus, const std::string& response);
			void call_dhbcalled(const std::string& call_number);
			void callback_dhbcalled(const uint32_t httpstatus, const std::string& response);

		protected:
			

		private:
			void request_params_check();
			void request_double_call();
			void request_auto_call();
			void request_create_auto_dialer_task();
			Json::Value getDidInfo(const std::string& did);
			Json::Value getLineInfo(const std::string& lineId);
			Json::Value getExternalRiskProvider(const std::string& id);

		private:
			ivsapi*m_owner;
			log4cplus::Logger log;
			const std::string m_Id;
			struct evhttp_request * m_req;
			Json::Value m_request_params;
			//string m_sessionId;
			FreeSwitch::FreeSwitchModule* m_fsmodule;
			string m_url;
			string m_mch_id;
			string m_apikey;
			string m_dhb_called_prefix;
		};

	}
}
