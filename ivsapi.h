#pragma once
#include "../http/http_server.h"
#include "../model/ProcessModule.h"
#include "../mysql/MySqlModule.h"
#include "../FreeSWITCH/FreeSwitchModule.h"

namespace chilli {
    namespace http {
    
        class RequestIVSAPI_DHB;
        class RequestIVSAPI_BlackList1;
        class RequestIVSAPI_BlackList2;
        class ivsapi :public model::ProcessModule, public http::http_server
        {
        public:
            explicit ivsapi(const std::string& id, uint32_t threadSize);
            ~ivsapi();

            virtual int Start() override;
            virtual int Stop() override;
            virtual bool LoadConfig(const std::string& configContext) override;
            virtual void run() override;
            virtual void execute(TexecuteThread* threadData) override;

            bool parse_string_to_json(const std::string& str, Json::Value& json);
            void write_response_success(struct evhttp_request* req, const Json::Value& data);
            void write_response_fail(struct evhttp_request* req, std::string messge = "");
            void write_response_fail2(struct evhttp_request* req, const Json::Value& data, std::string messge = "");

            void call_dhb(const Json::Value& param);
            void callback_dhb(const uint32_t httpstatus, const std::string& response, const std::string& sessionId);

            void call_blacklist1(const Json::Value& param);
            void callback_blacklist1(const uint32_t httpstatus, const std::string& response, const std::string& sessionId);

            void call_blacklist2(const Json::Value& param);
            void callback_blacklist2(const uint32_t httpstatus, const std::string& response, const std::string& sessionId);

        protected:
            void write_response(struct evhttp_request* req, const uint32_t& code, const std::string& messge, const Json::Value& data);


        private:
            virtual void fireSend(const fsm::FireDataType& fireData, const void* param) override;

            virtual const log4cplus::Logger& get_logger_http_server() override;
            virtual void http_callback_handle_generic(struct evhttp_request* req) override;

        private:
            std::string m_apikey;
            std::string m_url_interface;
            DataBase::MySqlModule* m_mysqlmodule = nullptr;
            FreeSwitch::FreeSwitchModule* m_fsModule = nullptr;
        };

    }
}


