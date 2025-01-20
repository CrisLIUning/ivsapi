#include <log4cplus/loggingmacros.h>
#include <json/json.h>
#include <log4cplus/loggingmacros.h>
#include "ivsapi_session.h"
#include "../uuid.h"
#include "request_dhbcaller.h"
#include "request_dhbcalled.h"
#include "../Agent/AgentModule.h"  // Must come first for complete type definition

namespace chilli {
	namespace http {
		ivsapi_session::ivsapi_session(ivsapi *owner, const std::string &session_id, const Json::Value &json_data, struct evhttp_request *req)
			:m_owner(owner), m_Id(session_id), m_request_params(json_data),m_req(req)
		{
			if (m_owner) {
				log = owner->getLogger();
				LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session construct");

				for (auto& m : model::ProcessModule::g_Modules)
				{
					if (m->getId().find("freeswitch001") != std::string::npos)
					{
						m_fsmodule = dynamic_cast<FreeSwitch::FreeSwitchModule*>(m.get());
					}
				}

				request_params_check();
			}
		}

		ivsapi_session::~ivsapi_session()
		{
			if (m_owner) {
				LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session deconstructor");
			}
		}

		const std::string ivsapi_session::getId()
		{
			return m_Id;
		}

		const log4cplus::Logger ivsapi_session::getLogger_agent_httpserver()
		{
			return log;
		}

		void ivsapi_session::request_params_check()
		{
			
			if (!m_request_params.isMember("method")) {
				m_owner->write_response_fail(m_req, "param method not exists,please input!");
				return;
			}

			std::string method = m_request_params["method"].asString(); 
			if (method == "DoubleCall") {
				request_double_call();
			}
			else if (method == "AutoCall") {
				request_auto_call();
			}
			else if (method == "CreateAutoDialerTask") {
				request_create_auto_dialer_task();
			}
			else
			{
				m_owner->write_response_fail(m_req, "param method not exists,please input!");
				return;
			}
		}

		void ivsapi_session::request_double_call()
		{
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session request_double_call begin");

			if (!m_request_params.isMember("caller")) {
				m_owner->write_response_fail(m_req, "参数caller为必填项");
				return;
			}

			if (!m_request_params.isMember("did")) {
				m_owner->write_response_fail(m_req, "参数did为必填项");
				return;
			}

			if (!m_request_params.isMember("called")) {
				m_owner->write_response_fail(m_req, "参数called为必填项");
				return;
			}

			std::string did = m_request_params["did"].isString()?m_request_params["did"].asString():"";
			if (did == "") {
				m_owner->write_response_fail(m_req, "参数did不能是空字符串");
				return;
			}
			//查询did
			Json::Value did_info = getDidInfo(did);
			if (did_info == Json::nullValue) {
				m_owner->write_response_fail(m_req, "参数did号码不存在");
				return;
			}			

			std::string caller = m_request_params["caller"].isString()?m_request_params["caller"].asString():"";
			std::string called = m_request_params["called"].isString()?m_request_params["called"].asString():"";
			m_dhb_called_prefix = "";
			if (did_info.isMember("dhb_called_prefix") && did_info["dhb_called_prefix"].isString()) m_dhb_called_prefix = did_info["dhb_called_prefix"].asString();
			std::string external_risk_provider_id = "";
			if(did_info.isMember("external_risk_provider_id") && did_info["external_risk_provider_id"].isString()) external_risk_provider_id = did_info["external_risk_provider_id"].asString();

			//电话邦风控验证
			Json::Value dhb_info = Json::nullValue;
			if (external_risk_provider_id != "") dhb_info = getExternalRiskProvider(external_risk_provider_id);

			bool no_risk_control = true;
			if (dhb_info != Json::nullValue) {
				LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::request_double_call dhb_info != Json::nullValue " << external_risk_provider_id);
				no_risk_control = false;
			}

			Json::Value newEvt;
			newEvt["id"] = this->m_Id;
			newEvt["request"] = "MakeCallRestful";
			newEvt["type"] = "request";
			newEvt["param"] = did_info;
			newEvt["param"]["caller"] = caller;
			newEvt["param"]["called"] = called;
			newEvt["param"]["chilli_callid"] = helper::uuid();
			newEvt["param"]["no_risk_control"] = no_risk_control;
			model::EventType_t evt(new model::_EventType(newEvt));
			m_fsmodule->PushEvent(evt);

			if (!no_risk_control) {
				//要保证风控事件在MakeCallRestful后 执行
				m_url = dhb_info["api_url"].asString();
				m_mch_id = dhb_info["api_secret"].asString();
				m_apikey = dhb_info["api_key"].asString();				

				call_dhbcaller(m_dhb_called_prefix + caller);
			}
			else
			{
				Json::Value data = Json::nullValue;
				data["callid"] = newEvt["param"]["chilli_callid"];
				m_owner->write_response_success(m_req, data);
			}			
		}

		Json::Value ivsapi_session::getDidInfo(const std::string & did)
		{
			Json::Value returnValue = Json::nullValue;

			DataBase::MySqlModule* mysqlmodule;
			for (auto& m : model::ProcessModule::g_Modules)
			{
				if (m->getId().find("mysql") != std::string::npos)
				{
					mysqlmodule = dynamic_cast<DataBase::MySqlModule*>(m.get());
					break;
				}
			}

			if (mysqlmodule) {
				std::string sql = "select t1.phone_number,t1.customer_id,t1.customer_trunk_id,t1.supplier_id,t1.supplier_trunk_id,t2.remote_sip_addr,t2.max_calls,t2.service_type,";
				sql.append("t4.name customer_name,t5.name supplier_name,t2.name supplier_trunk_name,t3.name customer_trunk_name,");
				sql.append("t1.call_cost_bill_id,t1.call_income_bill_id,");
				sql.append("t6.bill_type cost_bill_type,t6.bill_unit cost_bill_unit,t6.unit_fee cost_unit_fee,");
				sql.append("t7.bill_type income_bill_type,t7.bill_unit income_bill_unit,t7.unit_fee income_unit_fee,");
				sql.append("t3.need_record,t3.record_percent,t3.record_stereo,t3.record_start_type,");
				sql.append("t3.external_risk_provider_id,t3.dhb_called_prefix,t3.time_rule_id,t3.called_rule_id,t3.called_freq_rule_id,t3.caller_freq_rule_id,t3.black_list_id,t3.use_system_black_list,t3.white_list_id ");
				sql.append("from did_number t1 ");
				sql.append("left join supplier_trunk t2 on t1.supplier_trunk_id = t2.id ");
				sql.append("left join customer_trunk t3 on t1.customer_trunk_id = t3.id ");
				sql.append("left join customer_base t4 on t1.customer_id = t4.id ");
				sql.append("left join supplier_base t5 on t1.supplier_id = t5.id ");
				sql.append("left join bill_rule_realtime t6 on t1.call_cost_bill_id = t6.id ");
				sql.append("left join bill_rule_realtime t7 on t1.call_income_bill_id = t7.id ");
				sql.append("where t1.phone_number = '" + did + "' ");
				Json::Value result = mysqlmodule->executeQuery(sql, this->getId());
				if (result.size() > 0) {
					returnValue = result[0];
				}
			}

			return returnValue;

		}

		void ivsapi_session::request_auto_call()
		{
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session request_auto_call begin");

			// Check if this is a task status update
			if (m_request_params.isMember("action") && m_request_params["action"].asString() == "updateStatus") {
				if (!m_request_params.isMember("taskId")) {
					m_owner->write_response_fail(m_req, "参数taskId为必填项");
					return;
				}
				if (!m_request_params.isMember("status")) {
					m_owner->write_response_fail(m_req, "参数status为必填项");
					return;
				}
				
				std::string taskId = m_request_params["taskId"].asString();
				std::string status = m_request_params["status"].asString();
				
				// Validate status
				if (status != "ACTIVE" && status != "PAUSED" && status != "COMPLETED" && status != "CANCELLED") {
					m_owner->write_response_fail(m_req, "无效的状态值");
					return;
				}
				
				// Get MySQL module
				DataBase::MySqlModule* mysqlmodule = nullptr;
				for (auto& m : model::ProcessModule::g_Modules) {
					if (m->getId().find("mysql") != std::string::npos) {
						mysqlmodule = dynamic_cast<DataBase::MySqlModule*>(m.get());
						break;
					}
				}
				
				if (!mysqlmodule) {
					m_owner->write_response_fail(m_req, "系统错误：数据库连接失败");
					return;
				}
				
				// Update task status
				std::string sql = "UPDATE auto_dialer_tasks SET status = '";
				sql.append(status + "' WHERE task_id = '" + taskId + "'");
				
				if (mysqlmodule->executeUpdate(sql, this->getId()) > 0) {
					Json::Value data;
					data["taskId"] = taskId;
					data["status"] = status;
					m_owner->write_response_success(m_req, data);
				} else {
					m_owner->write_response_fail(m_req, "任务不存在或状态更新失败");
				}
				return;
			}

			if (!m_request_params.isMember("caller")) {
				m_owner->write_response_fail(m_req, "参数caller为必填项");
				return;
			}

			if (!m_request_params.isMember("called")) {
				m_owner->write_response_fail(m_req, "参数called为必填项");
				return;
			}

			if (!m_request_params.isMember("lineId")) {
				m_owner->write_response_fail(m_req, "参数lineId为必填项");
				return;
			}

			if (!m_request_params.isMember("skillId")) {
				m_owner->write_response_fail(m_req, "参数skillId为必填项");
				return;
			}

			std::string lineId = m_request_params["lineId"].isString() ? m_request_params["lineId"].asString() : "";
			if (lineId == "") {
				m_owner->write_response_fail(m_req, "参数lineId不能是空字符串");
				return;
			}

			//查询line信息
			Json::Value line_info = getLineInfo(lineId);
			if (line_info == Json::nullValue) {
				m_owner->write_response_fail(m_req, "参数lineId不存在");
				return;
			}

			std::string caller = m_request_params["caller"].isString() ? m_request_params["caller"].asString() : "";
			std::string called = m_request_params["called"].isString() ? m_request_params["called"].asString() : "";
			std::string skillId = m_request_params["skillId"].isString() ? m_request_params["skillId"].asString() : "";

			// Check for available agents in skill group
			Agent::AgentModule* agentmodule = nullptr;
			for (auto& m : model::ProcessModule::g_Modules)
			{
				if (m->getId().find("agent") != std::string::npos)
				{
					agentmodule = dynamic_cast<Agent::AgentModule*>(m.get());
					break;
				}
			}

			if (!agentmodule || !agentmodule->hasAvailableAgentsForSkill(skillId)) {
				m_owner->write_response_fail(m_req, "技能组中没有空闲坐席");
				return;
			}

			// Create call record
			DataBase::MySqlModule* mysqlmodule = nullptr;
			for (auto& m : model::ProcessModule::g_Modules)
			{
				if (m->getId().find("mysql") != std::string::npos)
				{
					mysqlmodule = dynamic_cast<DataBase::MySqlModule*>(m.get());
					break;
				}
			}

			if (mysqlmodule) {
				std::string chilli_callid = helper::uuid();
				std::string sql = "INSERT INTO call_callout_detail (call_id, caller, called, line_id, skill_id, call_type, create_time) VALUES ('";
				sql.append(chilli_callid + "', '");
				sql.append(caller + "', '");
				sql.append(called + "', '");
				sql.append(lineId + "', '");
				sql.append(skillId + "', '");
				sql.append("auto', '");
				sql.append(helper::time::getCurrentSystemTime() + "')");
				
				mysqlmodule->executeUpdate(sql, this->getId());

				Json::Value newEvt;
				newEvt["id"] = this->m_Id;
				newEvt["request"] = "MakeAutoCallRestful";
				newEvt["type"] = "request";
				newEvt["param"] = line_info;
				newEvt["param"]["caller"] = caller;
				newEvt["param"]["called"] = called;
				newEvt["param"]["skillId"] = skillId;
				newEvt["param"]["chilli_callid"] = chilli_callid;
				newEvt["param"]["auto_call"] = true;
				model::EventType_t evt(new model::_EventType(newEvt));
				m_fsmodule->PushEvent(evt);

				Json::Value data = Json::nullValue;
				data["callid"] = newEvt["param"]["chilli_callid"];
				m_owner->write_response_success(m_req, data);
			}
		}  // end of request_auto_call

		Json::Value ivsapi_session::getLineInfo(const std::string & lineId)
		{
			Json::Value returnValue = Json::nullValue;

			DataBase::MySqlModule* mysqlmodule;
			for (auto& m : model::ProcessModule::g_Modules)
			{
				if (m->getId().find("mysql") != std::string::npos)
				{
					mysqlmodule = dynamic_cast<DataBase::MySqlModule*>(m.get());
					break;
				}
			}

			if (mysqlmodule) {
				std::string sql = "select t1.id,t1.customer_id,t1.customer_trunk_id,t1.supplier_id,t1.supplier_trunk_id,t2.remote_sip_addr,t2.max_calls,t2.service_type,";
				sql.append("t4.name customer_name,t5.name supplier_name,t2.name supplier_trunk_name,t3.name customer_trunk_name,");
				sql.append("t1.call_cost_bill_id,t1.call_income_bill_id,");
				sql.append("t6.bill_type cost_bill_type,t6.bill_unit cost_bill_unit,t6.unit_fee cost_unit_fee,");
				sql.append("t7.bill_type income_bill_type,t7.bill_unit income_bill_unit,t7.unit_fee income_unit_fee,");
				sql.append("t3.need_record,t3.record_percent,t3.record_stereo,t3.record_start_type,");
				sql.append("t3.external_risk_provider_id,t3.dhb_called_prefix,t3.time_rule_id,t3.called_rule_id,t3.called_freq_rule_id,t3.caller_freq_rule_id,t3.black_list_id,t3.use_system_black_list,t3.white_list_id ");
				sql.append("from customer_trunk_line t1 ");
				sql.append("left join supplier_trunk t2 on t1.supplier_trunk_id = t2.id ");
				sql.append("left join customer_trunk t3 on t1.customer_trunk_id = t3.id ");
				sql.append("left join customer_base t4 on t1.customer_id = t4.id ");
				sql.append("left join supplier_base t5 on t1.supplier_id = t5.id ");
				sql.append("left join bill_rule_realtime t6 on t1.call_cost_bill_id = t6.id ");
				sql.append("left join bill_rule_realtime t7 on t1.call_income_bill_id = t7.id ");
				sql.append("where t1.id = '" + lineId + "' ");
				Json::Value result = mysqlmodule->executeQuery(sql, this->getId());
				if (result.size() > 0) {
					returnValue = result[0];
				}
			}

			return returnValue;
		}

		void ivsapi_session::request_create_auto_dialer_task()
		{
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session request_create_auto_dialer_task begin");

			// Validate required parameters
			if (!m_request_params.isMember("skillId")) {
				m_owner->write_response_fail(m_req, "参数skillId为必填项");
				return;
			}

			if (!m_request_params.isMember("lineId")) {
				m_owner->write_response_fail(m_req, "参数lineId为必填项");
				return;
			}

			if (!m_request_params.isMember("numbers") || !m_request_params["numbers"].isArray()) {
				m_owner->write_response_fail(m_req, "参数numbers为必填项且必须为数组");
				return;
			}

			std::string skillId = m_request_params["skillId"].isString() ? m_request_params["skillId"].asString() : "";
			std::string lineId = m_request_params["lineId"].isString() ? m_request_params["lineId"].asString() : "";
			int concurrencyRatio = m_request_params["concurrencyRatio"].isInt() ? m_request_params["concurrencyRatio"].asInt() : 5;

			// Validate line exists
			Json::Value line_info = getLineInfo(lineId);
			if (line_info == Json::nullValue) {
				m_owner->write_response_fail(m_req, "参数lineId不存在");
				return;
			}

			// Get MySQL module
			DataBase::MySqlModule* mysqlmodule = nullptr;
			for (auto& m : model::ProcessModule::g_Modules) {
				if (m->getId().find("mysql") != std::string::npos) {
					mysqlmodule = dynamic_cast<DataBase::MySqlModule*>(m.get());
					break;
				}
			}

			if (!mysqlmodule) {
				m_owner->write_response_fail(m_req, "系统错误：数据库连接失败");
				return;
			}

			// Generate task ID
			std::string taskId = helper::uuid();

			// Create task
			std::string sql = "INSERT INTO auto_dialer_tasks (task_id, skill_id, line_id, concurrency_ratio) VALUES ('";
			sql.append(taskId + "', '");
			sql.append(skillId + "', '");
			sql.append(lineId + "', ");
			sql.append(std::to_string(concurrencyRatio) + ")");

			mysqlmodule->executeUpdate(sql, this->getId());

			// Insert numbers
			const Json::Value& numbers = m_request_params["numbers"];
			for (const auto& number : numbers) {
				if (!number.isMember("caller") || !number.isMember("called")) {
					continue;
				}

				std::string caller = number["caller"].asString();
				std::string called = number["called"].asString();

				sql = "INSERT INTO auto_dialer_numbers (task_id, caller, called) VALUES ('";
				sql.append(taskId + "', '");
				sql.append(caller + "', '");
				sql.append(called + "')");

				mysqlmodule->executeUpdate(sql, this->getId());
			}

			// Return success with task ID
			Json::Value data;
			data["taskId"] = taskId;
			m_owner->write_response_success(m_req, data);
		}

		Json::Value ivsapi_session::getExternalRiskProvider(const std::string& id)
		{
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::getExternalRiskProvider id:" << id);
			Json::Value returnValue = Json::nullValue;

			DataBase::MySqlModule* mysqlmodule;
			for (auto& m : model::ProcessModule::g_Modules)
			{
				if (m->getId().find("mysql") != std::string::npos)
				{
					mysqlmodule = dynamic_cast<DataBase::MySqlModule*>(m.get());
					break;
				}
			}

			if (mysqlmodule) {
				std::string sql = "select t1.api_url,t1.api_key,t1.api_secret ";
				sql.append("from rule_external_risk_provider t1 ");
				sql.append("where t1.id = '" + id + "' ");
				Json::Value result = mysqlmodule->executeQuery(sql, this->getId());
				if (result.size() > 0) {
					returnValue = result[0];
				}
			}

			return returnValue;

		}

		void ivsapi_session::call_dhbcaller(const std::string& call_number)
		{						
			string params = "called_number="+ call_number +"&hash_type=1&mch_id="+ m_mch_id +"&apikey="+ m_apikey;
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::call_dhbcaller params:" << params);			

			string md5_string = md5(params);
			string sign = helper::string::toUpper(md5_string);
			string url = m_url + "?" + params + "&sign=" + sign;

			std::map<std::string, std::string> headers = {};// { { "Content-Type", "application/json; charset=UTF-8" } };
			request_dhbcaller requestDHBCaller(HttpRequest::http_method::HTTP_REQ_POST, url, headers, this);
			requestDHBCaller.Send();
		}

		void ivsapi_session::callback_dhbcaller(const uint32_t httpstatus, const std::string& response)
		{
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::callback_dhbcaller httpstatus:" << httpstatus);
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::callback_dhbcaller response:" << response);			

			bool isValid = false;
			Json::Value jsonResult;			
			if (httpstatus == 200) {
				
				if (m_owner->parse_string_to_json(response, jsonResult)) {
					if (jsonResult["status"].asInt() == 0) {

						if (!jsonResult["data"]["is_risk"].asBool()) {
							//验证通过
							isValid = true;
							call_dhbcalled(m_dhb_called_prefix + m_request_params["called"].asString());
							return;
						}

					}
					else
						LOG4CPLUS_ERROR(log, this->getId() << " ivsapi_session::callback_dhbcaller return status not 0:" << response);
				}
				
			}
			else if (httpstatus == 455) {
				m_owner->parse_string_to_json(response, jsonResult);
				LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::callback_dhbcaller httpstatus 455 response:" << response);
			}
			else
			{
				m_owner->parse_string_to_json(response, jsonResult);
			}

			jsonResult["data"]["caller"] = m_request_params["caller"].asString();
			m_owner->write_response_fail2(m_req, jsonResult, "操作失败");
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::callback_dhbcaller 操作失败:" << jsonResult << ",m_req =>" << m_req << ",m_req->evcon=" << m_req->evcon);

			Json::Value newEvt;
			newEvt["id"] = this->m_Id;
			newEvt["type"] = "event";
			newEvt["param"]["number"] = m_request_params["caller"].asString();
			newEvt["event"] = "RULE_EXTERNAL_RISK_FAIL";
			model::EventType_t evt(new model::_EventType(newEvt));
			m_fsmodule->PushEvent(evt);
		}

		void ivsapi_session::call_dhbcalled(const std::string& call_number)
		{
			string params = "called_number=" + call_number + "&hash_type=1&mch_id=" + m_mch_id + "&apikey=" + m_apikey;
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::call_dhbcalled params:" << params);
			string md5_string = md5(params);
			string sign = helper::string::toUpper(md5_string);
			string url = m_url + "?" + params + "&sign=" + sign;

			std::map<std::string, std::string> headers = {};// { { "Content-Type", "application/json; charset=UTF-8" } };
			request_dhbcalled requestDHBCalled(HttpRequest::http_method::HTTP_REQ_POST, url, headers, this);
			requestDHBCalled.Send();
		}

		void ivsapi_session::callback_dhbcalled(const uint32_t httpstatus, const std::string& response)
		{
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::call_dhbcalled httpstatus:" << httpstatus);
			LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::call_dhbcalled response:" << response);
			bool isValid = false;
			Json::Value jsonResult;
			if (httpstatus == 200) {
				
				if (m_owner->parse_string_to_json(response, jsonResult)) {
					if (jsonResult["status"].asInt() == 0) {

						if (!jsonResult["data"]["is_risk"].asBool()) {
							//验证通过
							isValid = true;
						}

					}
					else
						LOG4CPLUS_ERROR(log, this->getId() << " ivsapi_session::call_dhbcalled return failed:" << response);
				}
				else
					LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::call_dhbcalled reader.parse fail.");
			}
			else
			{
				m_owner->parse_string_to_json(response, jsonResult);
			}

			jsonResult["data"]["called"] = m_request_params["called"].asString();
			if (isValid) {
				m_owner->write_response_success(m_req, jsonResult["data"]);
				LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::call_dhbcalled 操作成功。");
			}
			else
			{
				m_owner->write_response_fail2(m_req, jsonResult, "操作失败");
				LOG4CPLUS_TRACE(log, this->getId() << " ivsapi_session::call_dhbcalled 操作失败:" << jsonResult);
			}

			Json::Value newEvt;
			newEvt["id"] = this->m_Id;
			newEvt["type"] = "event";
			newEvt["param"]["number"] = m_request_params["called"].asString();
			newEvt["event"] = isValid ? "RULE_EXTERNAL_RISK_SUCC" : "RULE_EXTERNAL_RISK_FAIL";
			model::EventType_t evt(new model::_EventType(newEvt));
			m_fsmodule->PushEvent(evt);
		}
	}
}

