#include "dmtcpmessagetypes.h"
#include "json.h"

#include "unit_test.h"

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

namespace {

void coordinatorCmdNamesMatchEnumNames()
{
  ASSERT_EQ(std::string("DMT_STATUS"),
            std::string(dmtcp::coordinatorCmdName(dmtcp::DMT_STATUS)));
  ASSERT_EQ(std::string("DMT_BLOCKING_CKPT"),
            std::string(dmtcp::coordinatorCmdName(
              dmtcp::DMT_BLOCKING_CKPT)));
  ASSERT_EQ(std::string("DMT_COORD_NOT_RUNNING"),
            std::string(dmtcp::coordinatorCmdStatusName(
              dmtcp::DMT_COORD_NOT_RUNNING)));
}

void jsonBuilderSerializesSupportedFieldTypes()
{
  dmtcp::Json json;
  char mutableString[] = "mutable";
  const char *nullString = NULL;

  json.appendField("int_value", -7);
  json.appendField("uint_value", static_cast<uint64_t>(42));
  json.appendField("signed_wide", static_cast<int64_t>(-5000000000LL));
  json.appendField("small_uint", static_cast<uint16_t>(65535));
  json.appendField("bool_value", true);
  json.appendField("string_value", "line\nquote\"");
  json.appendField("mutable_string", mutableString);
  json.appendField("null_string", nullString);
  json.appendField("view_value", std::string_view("view"));

  ASSERT_EQ(
    std::string(
      "{\"int_value\":-7,\"uint_value\":42,"
      "\"signed_wide\":-5000000000,\"small_uint\":65535,"
      "\"bool_value\":true,"
      "\"string_value\":\"line\\nquote\\\"\","
      "\"mutable_string\":\"mutable\",\"null_string\":\"\","
      "\"view_value\":\"view\"}"),
    std::string(json.str().c_str()));
}

void statusResponseSerializesToCoordinatorJson()
{
  dmtcp::DmtcpMessage response(dmtcp::DMT_USER_CMD_RESULT);
  response.coordCmd = dmtcp::DMT_STATUS;
  response.numPeers = 3;
  response.isRunning = 1;
  response.theCheckpointInterval = 42;

  dmtcp::string json =
    response.toCoordinatorCmdJson("localhost", 7779);

  ASSERT_EQ(
    std::string(
      "{\"schema_version\":1,\"command\":\"DMT_STATUS\","
      "\"command_status\":\"DMT_COORD_SUCCESS\","
      "\"coordinator_host\":\"localhost\","
      "\"coordinator_port\":7779,\"num_peers\":3,\"running\":true,"
      "\"checkpoint_interval\":42}"),
    std::string(json.c_str()));
}

void coordinatorErrorSerializesToCoordinatorJson()
{
  dmtcp::DmtcpMessage response(dmtcp::DMT_USER_CMD_RESULT);
  response.coordCmd = dmtcp::DMT_CHECKPOINT;
  response.coordCmdStatus = dmtcp::DMT_COORD_NOT_RUNNING;

  dmtcp::string json = response.toCoordinatorCmdJson("localhost", 7779);

  ASSERT_EQ(
    std::string(
      "{\"schema_version\":1,\"command\":\"DMT_CHECKPOINT\","
      "\"command_status\":\"DMT_COORD_NOT_RUNNING\","
      "\"coordinator_host\":\"localhost\",\"coordinator_port\":7779}"),
    std::string(json.c_str()));
}

void coordinatorJsonEscapesHost()
{
  dmtcp::DmtcpMessage response(dmtcp::DMT_USER_CMD_RESULT);
  response.coordCmd = dmtcp::DMT_STATUS;
  response.coordCmdStatus = dmtcp::DMT_COORD_NOT_FOUND;

  dmtcp::string json =
    response.toCoordinatorCmdJson("bad\"host\nname", 1);

  ASSERT_EQ(
    std::string(
      "{\"schema_version\":1,\"command\":\"DMT_STATUS\","
      "\"command_status\":\"DMT_COORD_NOT_FOUND\","
      "\"coordinator_host\":\"bad\\\"host\\nname\","
      "\"coordinator_port\":1}"),
    std::string(json.c_str()));
}

void listResponseSerializesToCoordinatorJson()
{
  dmtcp::DmtcpMessage response(dmtcp::DMT_USER_CMD_RESULT);
  response.coordCmd = dmtcp::DMT_LIST;

  dmtcp::string json =
    response.toCoordinatorCmdJson("localhost", 7779, "worker list\n");

  ASSERT_EQ(
    std::string(
      "{\"schema_version\":1,\"command\":\"DMT_LIST\","
      "\"command_status\":\"DMT_COORD_SUCCESS\","
      "\"coordinator_host\":\"localhost\",\"coordinator_port\":7779,"
      "\"workers\":\"worker list\\n\"}"),
    std::string(json.c_str()));
}

void intervalResponseSerializesToCoordinatorJson()
{
  dmtcp::DmtcpMessage response(dmtcp::DMT_USER_CMD_RESULT);
  response.coordCmd = dmtcp::DMT_UPDATE_CKPT_INTERVAL;
  response.theCheckpointInterval = 7;

  dmtcp::string json =
    response.toCoordinatorCmdJson("localhost", 7779);

  ASSERT_EQ(
    std::string(
      "{\"schema_version\":1,\"command\":\"DMT_UPDATE_CKPT_INTERVAL\","
      "\"command_status\":\"DMT_COORD_SUCCESS\","
      "\"coordinator_host\":\"localhost\",\"coordinator_port\":7779,"
      "\"checkpoint_interval\":7}"),
    std::string(json.c_str()));
}

void helpResponseSerializesToCoordinatorJson()
{
  dmtcp::DmtcpMessage response(dmtcp::DMT_USER_CMD_RESULT);
  response.coordCmd = dmtcp::DMT_HELP;

  dmtcp::string json =
    response.toCoordinatorCmdJson("localhost", 7779);

  ASSERT_EQ(
    std::string(
      "{\"schema_version\":1,\"command\":\"DMT_HELP\","
      "\"command_status\":\"DMT_COORD_SUCCESS\","
      "\"coordinator_host\":\"localhost\",\"coordinator_port\":7779}"),
    std::string(json.c_str()));
}

} // namespace

extern const dmtcp_test::TestCase dmtcpMessageTests[] = {
  {"coordinator command names match enum names",
   coordinatorCmdNamesMatchEnumNames},
  {"JSON builder serializes supported field types",
   jsonBuilderSerializesSupportedFieldTypes},
  {"status response serializes to coordinator JSON",
   statusResponseSerializesToCoordinatorJson},
  {"coordinator error serializes to coordinator JSON",
   coordinatorErrorSerializesToCoordinatorJson},
  {"coordinator JSON escapes host", coordinatorJsonEscapesHost},
  {"list response serializes to coordinator JSON",
   listResponseSerializesToCoordinatorJson},
  {"interval response serializes to coordinator JSON",
   intervalResponseSerializesToCoordinatorJson},
  {"help response serializes to coordinator JSON",
   helpResponseSerializesToCoordinatorJson},
};

extern const size_t dmtcpMessageTestCount =
  sizeof(dmtcpMessageTests) / sizeof(dmtcpMessageTests[0]);
