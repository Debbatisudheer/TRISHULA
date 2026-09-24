#include <iostream>
#include <stdexcept>
#include <string>
#include "trishula/ground/command_execution.h"

namespace {
using namespace trishula;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
GroundCommandRecord command(const char* id, const char* target, const char* opcode) {
    GroundCommandRecord value{};
    value.command_id=id; value.source_node="MISSION_CONTROL"; value.destination_node=target;
    value.target=target; value.opcode=opcode; value.application_id=210U;
    value.mission_timestamp_ns=123456789ULL; value.sequence_number=1U; return value;
}
void test_executed_ack() {
    GroundCommandExecutionEngine engine;
    const auto result=engine.execute(command("CMD-ACK-0001","ROVER","START_ROVER"));
    require(result.status==CommandExecutionStatus::Executed,"command did not execute");
    require(result.acknowledgement.status==CommandAcknowledgementStatus::Executed,"ack status not EXECUTED");
    require(result.acknowledgement.command_id=="CMD-ACK-0001","ack command id mismatch");
    const std::string payload(result.acknowledgement_packet.payload.begin(),result.acknowledgement_packet.payload.end());
    require(payload.find("event=COMMAND_ACKNOWLEDGEMENT")!=std::string::npos,"ack event missing");
    require(payload.find("status=EXECUTED")!=std::string::npos,"executed status missing");
}
void test_rejected_ack() {
    GroundCommandExecutionEngine engine;
    const auto result=engine.execute(command("CMD-ACK-0002","UNKNOWN","STOP"));
    require(result.status==CommandExecutionStatus::TargetUnavailable,"invalid target unexpectedly executed");
    require(result.acknowledgement.status==CommandAcknowledgementStatus::Rejected,"rejected ack not generated");
    const std::string payload(result.acknowledgement_packet.payload.begin(),result.acknowledgement_packet.payload.end());
    require(payload.find("status=REJECTED")!=std::string::npos,"rejected status missing");
}
void test_event_status_matches_execution() {
    GroundCommandExecutionEngine engine;
    const auto result=engine.execute(command("CMD-ACK-0003","ROVER","NOT_SUPPORTED"));
    const std::string payload(result.event_packet.payload.begin(),result.event_packet.payload.end());
    require(result.status==CommandExecutionStatus::UnsupportedOpcode,"unsupported opcode status mismatch");
    require(payload.find("status=UNSUPPORTED_OPCODE")!=std::string::npos,"event status incorrectly reported execution success");
}
}
int main() {
    try {
        test_executed_ack(); test_rejected_ack(); test_event_status_matches_execution();
        std::cout << "TRISHULA V0.9.53 - Ground Command Acknowledgement & Operational Feedback\n"
                  << "===============================================================\n"
                  << "  executed acknowledgement generated : PASS\n"
                  << "  rejected acknowledgement generated : PASS\n"
                  << "  event status reflects result       : PASS\n"
                  << "  command id correlation preserved   : PASS\n"
                  << "  campaign                            : PASS\n\n"
                  << "V0.9.53 campaign PASSED.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "V0.9.53 campaign failed: " << e.what() << '\n'; return 1; }
}
