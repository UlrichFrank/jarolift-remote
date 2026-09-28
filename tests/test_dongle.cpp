#include <doctest/doctest.h>

#include "core_dongle.h"

using namespace esphome::jarolift;

// Format of the old dongle's html_api.h "get config" answer.
static const char *CONFIG =
    "ssid=automation\npassword=secret-wifi\ncheckbox=dhcp=1\nip_0=10\nip_1=0\nip_2=10\nip_3=15\n"
    "mqtt_broker_addr_0=10\nmqtt_broker_port=1883\nmqtt_devicetopic=jarolift\n"
    "master_msb=0x01234567\nmaster_lsb=0x89abcdef\ncheckbox=learn_mode=1\nserial=0x12deaf\n"
    "checkbox=set_and_generate_serial=0\ndevicecounter=27018\ncheckbox=set_devicecounter=0\n"
    "text=versionstr=Firmware v0.6\n";

TEST_CASE("parse the dongle's configuration") {
  DongleConfig d;
  std::string err;
  REQUIRE(parse_dongle_config(CONFIG, &d, &err));
  CHECK(d.master_msb == 0x01234567);
  CHECK(d.master_lsb == 0x89abcdef);
  CHECK(d.serial_prefix == 0x12deaf);
  CHECK(d.learn_mode_new);
  CHECK(d.device_counter == 27018);
}

TEST_CASE("old learn mode and CRLF lines") {
  DongleConfig d;
  std::string err;
  REQUIRE(parse_dongle_config("master_msb=0x1\r\nmaster_lsb=0x2\r\nserial=0x3\r\ndevicecounter=5\r\ncheckbox=learn_mode=0\r\n", &d, &err));
  CHECK_FALSE(d.learn_mode_new);
  CHECK(d.device_counter == 5);
}

TEST_CASE("incomplete or malformed configuration is rejected") {
  DongleConfig d;
  std::string err;
  CHECK_FALSE(parse_dongle_config("master_msb=0x1\nmaster_lsb=0x2\nserial=0x3\n", &d, &err));
  CHECK(err.find("counter") != std::string::npos);
  CHECK_FALSE(parse_dongle_config("master_msb=zz\nmaster_lsb=0x2\nserial=0x3\ndevicecounter=1\n", &d, &err));
  CHECK(err.find("master") != std::string::npos);
  CHECK_FALSE(parse_dongle_config("master_msb=0x1\nmaster_lsb=0x2\nserial=0x1234567\ndevicecounter=1\n", &d, &err));
  CHECK_FALSE(parse_dongle_config("master_msb=0x1\nmaster_lsb=0x2\nserial=0x3\ndevicecounter=70000\n", &d, &err));
  CHECK_FALSE(parse_dongle_config("<html>not the api</html>", &d, &err));
}

TEST_CASE("channel names") {
  DongleConfig d;
  parse_dongle_channel_names("channel_0=EG Küche Fenster\nchannel_9=Alle\nchannel_14=\nchannel_15=\nchannel_99=x\n", &d);
  CHECK(d.channel_names[0] == "EG Küche Fenster");
  CHECK(d.channel_names[9] == "Alle");
  CHECK(d.channel_names[14].empty());
}
