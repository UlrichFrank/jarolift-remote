import gzip
from pathlib import Path
import re

import esphome.codegen as cg
from esphome.components import number, socket
from esphome.components.logger import request_log_listener
from esphome.components.esp32 import add_idf_sdkconfig_option, include_builtin_idf_component
from esphome.components.time_based.cover import TimeBasedCover
import esphome.config_validation as cv
from esphome.const import CONF_CHANNEL, CONF_ID, CONF_NAME, CONF_PASSWORD, CONF_USERNAME

CODEOWNERS = ["@UlrichFrank"]
DEPENDENCIES = ["network", "esp32"]
AUTO_LOAD = ["json"]

CONF_MASTER_MSB = "master_msb"
CONF_MASTER_LSB = "master_lsb"
CONF_SERIAL_PREFIX = "serial_prefix"
CONF_LEARN_MODE_NEW = "learn_mode_new"
CONF_INITIAL_COUNTER = "initial_counter"
CONF_WEB = "web"
CONF_MQTT = "mqtt"
CONF_BROKER_URL = "broker_url"
CONF_NETWORK = "network"
CONF_STATIC_IP = "static_ip"
CONF_GATEWAY = "gateway"
CONF_SUBNET = "subnet"
CONF_DNS = "dns"
CONF_SHUTTERS = "shutters"
CONF_GROUPS = "groups"
CONF_MEMBERS = "members"
CONF_COVER = "cover"
CONF_OPEN_DURATION = "open_duration"
CONF_CLOSE_DURATION = "close_duration"
CONF_SLOTS = "slots"
CONF_LABEL = "label"
CONF_DHCP = "dhcp"

NUM_CHANNELS = 16
MAX_GROUPS = 8

jarolift_ns = cg.esphome_ns.namespace("jarolift")
JaroliftComponent = jarolift_ns.class_("JaroliftComponent", cg.Component)
NetworkBootstrap = jarolift_ns.class_("NetworkBootstrap", cg.Component)
CONF_BOOTSTRAP_ID = "network_bootstrap_id"
CONF_RESET = "reset"


def hex_bits(bits):
    def validator(value):
        value = cv.string_strict(value)
        try:
            number_ = int(value, 16)
        except ValueError as err:
            raise cv.Invalid(f"'{value}' is not a hexadecimal number") from err
        if not 0 <= number_ < (1 << bits):
            raise cv.Invalid(f"'{value}' does not fit into {bits} bits")
        return number_

    return validator


def topic_name(value):
    value = cv.string_strict(value)
    if not re.fullmatch(r"[a-z0-9]+(-[a-z0-9]+)*", value):
        raise cv.Invalid(f"'{value}' must be kebab-case (a-z, 0-9, '-'), it becomes an MQTT topic level")
    if value == "bridge":
        raise cv.Invalid("'bridge' is reserved for jarolift/bridge/state")
    return value


CHANNEL = cv.int_range(min=0, max=NUM_CHANNELS - 1)

# Hardware of each channel: one time-based cover and two duration numbers (packages/channel.yaml).
SLOT_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_CHANNEL): CHANNEL,
        cv.Required(CONF_COVER): cv.use_id(TimeBasedCover),
        cv.Required(CONF_OPEN_DURATION): cv.use_id(number.Number),
        cv.Required(CONF_CLOSE_DURATION): cv.use_id(number.Number),
    }
)

# Initial channel layout; the web interface can change it at runtime.
SHUTTER_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): topic_name,
        cv.Required(CONF_CHANNEL): CHANNEL,
        cv.Optional(CONF_LABEL, default=""): cv.string,
    }
)

# A group is the bitmask of its members' channels (one telegram for all), not a channel of its own.
GROUP_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): topic_name,
        cv.Optional(CONF_LABEL, default=""): cv.string,
        cv.Required(CONF_MEMBERS): cv.All(cv.ensure_list(topic_name), cv.Length(min=1)),
    }
)


def validate_channels(config):
    slots = sorted(slot[CONF_CHANNEL] for slot in config[CONF_SLOTS])
    if slots != list(range(NUM_CHANNELS)):
        raise cv.Invalid(f"slots must list every channel 0..{NUM_CHANNELS - 1} exactly once")
    if len(config[CONF_GROUPS]) > MAX_GROUPS:
        raise cv.Invalid(f"at most {MAX_GROUPS} groups")
    names, channels = set(), set()
    shutter_names = {s[CONF_NAME] for s in config[CONF_SHUTTERS]}
    for entry in config[CONF_SHUTTERS] + config[CONF_GROUPS]:
        if entry[CONF_NAME] in names:
            raise cv.Invalid(f"name '{entry[CONF_NAME]}' is used twice")
        names.add(entry[CONF_NAME])
    for entry in config[CONF_SHUTTERS]:
        if entry[CONF_CHANNEL] in channels:
            raise cv.Invalid(f"channel {entry[CONF_CHANNEL]} is used twice")
        channels.add(entry[CONF_CHANNEL])
    for group in config[CONF_GROUPS]:
        for member in group[CONF_MEMBERS]:
            if member not in shutter_names:
                raise cv.Invalid(f"group '{group[CONF_NAME]}': unknown member '{member}'")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(JaroliftComponent),
            cv.GenerateID(CONF_BOOTSTRAP_ID): cv.declare_id(NetworkBootstrap),
            cv.Required(CONF_MASTER_MSB): hex_bits(32),
            cv.Required(CONF_MASTER_LSB): hex_bits(32),
            cv.Required(CONF_SERIAL_PREFIX): hex_bits(24),
            cv.Optional(CONF_LEARN_MODE_NEW, default=True): cv.boolean,
            cv.Optional(CONF_INITIAL_COUNTER, default=0): cv.int_range(min=0, max=0xFFFF),
            cv.Required(CONF_WEB): cv.Schema(
                {
                    cv.Required(CONF_USERNAME): cv.string_strict,
                    cv.Required(CONF_PASSWORD): cv.string_strict,
                }
            ),
            cv.Required(CONF_MQTT): cv.Schema(
                {
                    cv.Optional(CONF_BROKER_URL, default=""): cv.string,
                    cv.Optional(CONF_USERNAME, default=""): cv.string,
                    cv.Optional(CONF_PASSWORD, default=""): cv.string,
                }
            ),
            # Defaults for the web interface's network settings; DHCP unless a static address is saved.
            cv.Optional(CONF_NETWORK, default={}): cv.Schema(
                {
                    cv.Optional(CONF_DHCP, default=True): cv.boolean,
                    # Recovery by USB flash: forget a saved static configuration, use DHCP again.
                    cv.Optional(CONF_RESET, default=False): cv.boolean,
                    cv.Optional(CONF_STATIC_IP, default="0.0.0.0"): cv.ipv4address,
                    cv.Optional(CONF_GATEWAY, default="0.0.0.0"): cv.ipv4address,
                    cv.Optional(CONF_SUBNET, default="255.255.255.0"): cv.ipv4address,
                    cv.Optional(CONF_DNS, default="0.0.0.0"): cv.ipv4address,
                }
            ),
            cv.Required(CONF_SLOTS): cv.ensure_list(SLOT_SCHEMA),
            cv.Optional(CONF_SHUTTERS, default=[]): cv.ensure_list(SHUTTER_SCHEMA),
            cv.Optional(CONF_GROUPS, default=[]): cv.ensure_list(GROUP_SCHEMA),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_channels,
    # Not counted by ESPHome otherwise: the MQTT connection plus one while reconnecting; the HTTPS
    # server (3 clients) and the redirect server on 80 (2 clients), their listeners and the
    # httpd control sockets.
    socket.consume_sockets(2, "jarolift_mqtt"),
    socket.consume_sockets(5, "jarolift_web"),
    socket.consume_sockets(2, "jarolift_web", socket.SocketType.TCP_LISTEN),
    socket.consume_sockets(2, "jarolift_web", socket.SocketType.UDP),
)


def ip_int(address):
    a, b, c, d = (int(x) for x in str(address).split("."))
    return (a << 24) | (b << 16) | (c << 8) | d


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    bootstrap = cg.new_Pvariable(config[CONF_BOOTSTRAP_ID], var)
    await cg.register_component(bootstrap, {})

    include_builtin_idf_component("mqtt")
    include_builtin_idf_component("tcp_transport")
    include_builtin_idf_component("esp_http_server")
    include_builtin_idf_component("esp_https_server")
    include_builtin_idf_component("esp_http_client")  # takeover: reads the old dongle
    add_idf_sdkconfig_option("CONFIG_ESP_HTTPS_SERVER_ENABLE", True)
    add_idf_sdkconfig_option("CONFIG_HTTPD_MAX_REQ_HDR_LEN", 1024)
    add_idf_sdkconfig_option("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE", True)
    add_idf_sdkconfig_option("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL", True)
    add_idf_sdkconfig_option("CONFIG_MQTT_TRANSPORT_WEBSOCKET", True)
    add_idf_sdkconfig_option("CONFIG_MQTT_TRANSPORT_WEBSOCKET_SECURE", True)
    request_log_listener()  # log lines for the web interface's Log page

    def default(field, value):
        cg.add(cg.RawExpression(f"{var}->defaults().{field} = {value}"))

    net = config[CONF_NETWORK]
    default("dhcp", "true" if net[CONF_DHCP] else "false")
    cg.add(var.set_reset_network(net[CONF_RESET]))
    # Compiles EthernetComponent::set_manual_ip(); without a call it stays on DHCP.
    cg.add_define("USE_ETHERNET_MANUAL_IP")
    default("ip", ip_int(net[CONF_STATIC_IP]))
    default("gateway", ip_int(net[CONF_GATEWAY]))
    default("subnet", ip_int(net[CONF_SUBNET]))
    default("dns", ip_int(net[CONF_DNS]))
    mqtt = config[CONF_MQTT]
    default("broker_url", cg.safe_exp(mqtt[CONF_BROKER_URL]))
    default("mqtt_username", cg.safe_exp(mqtt[CONF_USERNAME]))
    default("mqtt_password", cg.safe_exp(mqtt[CONF_PASSWORD]))
    default("master_msb", config[CONF_MASTER_MSB])
    default("master_lsb", config[CONF_MASTER_LSB])
    default("serial_prefix", config[CONF_SERIAL_PREFIX])
    default("learn_mode_new", "true" if config[CONF_LEARN_MODE_NEW] else "false")
    cg.add(var.set_initial_counter(config[CONF_INITIAL_COUNTER]))
    web = config[CONF_WEB]
    cg.add(var.set_web_credentials(web[CONF_USERNAME], web[CONF_PASSWORD]))

    page = gzip.compress((Path(__file__).parent / "www" / "index.html").read_bytes(), mtime=0)
    data = ",".join(str(b) for b in page)
    cg.add_global(
        cg.RawStatement(
            "namespace esphome { namespace jarolift { "
            f"extern const uint8_t INDEX_HTML_GZ[] = {{{data}}}; "
            f"extern const size_t INDEX_HTML_GZ_LEN = {len(page)}; "
            "} }"
        )
    )

    for slot in config[CONF_SLOTS]:
        cover = await cg.get_variable(slot[CONF_COVER])
        open_duration = await cg.get_variable(slot[CONF_OPEN_DURATION])
        close_duration = await cg.get_variable(slot[CONF_CLOSE_DURATION])
        cg.add(var.add_slot(slot[CONF_CHANNEL], cover, open_duration, close_duration))

    channel_of = {s[CONF_NAME]: s[CONF_CHANNEL] for s in config[CONF_SHUTTERS]}
    for shutter in config[CONF_SHUTTERS]:
        ch = shutter[CONF_CHANNEL]
        default(f"channels[{ch}].enabled", "true")
        default(f"channels[{ch}].name", cg.safe_exp(shutter[CONF_NAME]))
        default(f"channels[{ch}].label", cg.safe_exp(shutter[CONF_LABEL]))
    for index, group in enumerate(config[CONF_GROUPS]):
        mask = 0
        for member in group[CONF_MEMBERS]:
            mask |= 1 << channel_of[member]
        default(f"groups[{index}].enabled", "true")
        default(f"groups[{index}].name", cg.safe_exp(group[CONF_NAME]))
        default(f"groups[{index}].label", cg.safe_exp(group[CONF_LABEL]))
        default(f"groups[{index}].members", mask)
