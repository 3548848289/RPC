// Generated by the MyRPC compiler v1.0.0 . DO NOT EDIT! unless you know what you are doing

#pragma once

#include <set>
#include <string>

#include "../../common/log.hpp"
#include "../../common/utils.hpp"
#include "../../core/handler.hpp"
#include "../../core/mysvrclient.hpp"
#include "../../protocol/base.pb.h"
#include "proto/authstore.pb.h"

using namespace MySvr::Base;
using namespace MySvr::AuthStore;

class AuthStoreHandler : public Core::MyHandler{
public:
    AuthStoreHandler() {
        service_name_ = std::string{"AuthStore"};
        rpc_names_ = std::unordered_set<std::string>{"SetTicket", "GetTicket"};
    }

    void MySvrHandler(Protocol::MySvrMessage &req, Protocol::MySvrMessage &resp) {
        RPC_HANDLER("SetTicket", SetTicket, SetTicketRequest, SetTicketResponse, req, resp);
        RPC_HANDLER("GetTicket", GetTicket, GetTicketRequest, GetTicketResponse, req, resp);
    }
    
    int SetTicket(SetTicketRequest &request, SetTicketResponse &response);
    int GetTicket(GetTicketRequest &request, GetTicketResponse &response);
};
