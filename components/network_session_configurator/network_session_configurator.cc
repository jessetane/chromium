// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/network_session_configurator/network_session_configurator.h"

#include <map>

#include "base/metrics/field_trial.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/data_reduction_proxy/core/common/data_reduction_proxy_params.h"
#include "components/network_session_configurator/switches.h"
#include "components/variations/variations_associated_data.h"
#include "components/version_info/version_info.h"
#include "net/http/http_stream_factory.h"
#include "net/quic/core/quic_protocol.h"
#include "net/quic/core/quic_utils.h"
#include "net/url_request/url_fetcher.h"

namespace {

// Map from name to value for all parameters associate with a field trial.
using VariationParameters = std::map<std::string, std::string>;

const char kTCPFastOpenFieldTrialName[] = "TCPFastOpen";
const char kTCPFastOpenHttpsEnabledGroupName[] = "HttpsEnabled";

const char kQuicFieldTrialName[] = "QUIC";
const char kQuicFieldTrialEnabledGroupName[] = "Enabled";
const char kQuicFieldTrialHttpsEnabledGroupName[] = "HttpsEnabled";

// Field trial for HTTP/2.
const char kHttp2FieldTrialName[] = "HTTP2";
const char kHttp2FieldTrialDisablePrefix[] = "Disable";

// Returns the value associated with |key| in |params| or "" if the
// key is not present in the map.
const std::string& GetVariationParam(
    const std::map<std::string, std::string>& params,
    const std::string& key) {
  std::map<std::string, std::string>::const_iterator it = params.find(key);
  if (it == params.end())
    return base::EmptyString();

  return it->second;
}

void ConfigureTCPFastOpenParams(base::StringPiece tfo_trial_group,
                                net::HttpNetworkSession::Params* params) {
  if (tfo_trial_group == kTCPFastOpenHttpsEnabledGroupName)
    params->enable_tcp_fast_open_for_ssl = true;
}

void ConfigureHttp2Params(const base::CommandLine& command_line,
                          base::StringPiece http2_trial_group,
                          net::HttpNetworkSession::Params* params) {
  if (command_line.HasSwitch(switches::kIgnoreUrlFetcherCertRequests))
    net::URLFetcher::SetIgnoreCertificateRequests(true);

  if (command_line.HasSwitch(switches::kDisableHttp2)) {
    params->enable_http2 = false;
    return;
  }

  if (http2_trial_group.starts_with(kHttp2FieldTrialDisablePrefix)) {
    params->enable_http2 = false;
  }
}

bool ShouldEnableQuic(const base::CommandLine& command_line,
                      base::StringPiece quic_trial_group,
                      bool is_quic_allowed_by_policy) {
  if (command_line.HasSwitch(switches::kDisableQuic) ||
      !is_quic_allowed_by_policy)
    return false;

  if (command_line.HasSwitch(switches::kEnableQuic))
    return true;

  return quic_trial_group.starts_with(kQuicFieldTrialEnabledGroupName) ||
         quic_trial_group.starts_with(kQuicFieldTrialHttpsEnabledGroupName);
}

bool ShouldDisableQuicWhenConnectionTimesOutWithOpenStreams(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params,
                        "disable_quic_on_timeout_with_open_streams"),
      "true");
}

bool ShouldQuicDisableConnectionPooling(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "disable_connection_pooling"),
      "true");
}

bool ShouldQuicEnableAlternativeServicesForDifferentHost(
    const base::CommandLine& command_line,
    const VariationParameters& quic_trial_params) {
  return !base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params,
                        "enable_alternative_service_with_different_host"),
      "false");
}

bool ShouldEnableQuicPortSelection(const base::CommandLine& command_line) {
  if (command_line.HasSwitch(switches::kDisableQuicPortSelection))
    return false;

  if (command_line.HasSwitch(switches::kEnableQuicPortSelection))
    return true;

  return false;  // Default to disabling port selection on all channels.
}

net::QuicTagVector GetQuicConnectionOptions(
    const base::CommandLine& command_line,
    const VariationParameters& quic_trial_params) {
  if (command_line.HasSwitch(switches::kQuicConnectionOptions)) {
    return net::QuicUtils::ParseQuicConnectionOptions(
        command_line.GetSwitchValueASCII(switches::kQuicConnectionOptions));
  }

  VariationParameters::const_iterator it =
      quic_trial_params.find("connection_options");
  if (it == quic_trial_params.end()) {
    return net::QuicTagVector();
  }

  return net::QuicUtils::ParseQuicConnectionOptions(it->second);
}

bool ShouldQuicAlwaysRequireHandshakeConfirmation(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params,
                        "always_require_handshake_confirmation"),
      "true");
}

float GetQuicLoadServerInfoTimeoutSrttMultiplier(
    const VariationParameters& quic_trial_params) {
  double value;
  if (base::StringToDouble(
          GetVariationParam(quic_trial_params, "load_server_info_time_to_srtt"),
          &value)) {
    return static_cast<float>(value);
  }
  return 0.0f;
}

bool ShouldQuicEnableConnectionRacing(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "enable_connection_racing"), "true");
}

bool ShouldQuicEnableNonBlockingIO(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "enable_non_blocking_io"), "true");
}

bool ShouldQuicDisableDiskCache(const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "disable_disk_cache"), "true");
}

bool ShouldQuicPreferAes(const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "prefer_aes"), "true");
}

bool ShouldForceHolBlocking(const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "force_hol_blocking"), "true");
}

int GetQuicMaxNumberOfLossyConnections(
    const VariationParameters& quic_trial_params) {
  int value;
  if (base::StringToInt(GetVariationParam(quic_trial_params,
                                          "max_number_of_lossy_connections"),
                        &value)) {
    return value;
  }
  return 0;
}

float GetQuicPacketLossThreshold(const VariationParameters& quic_trial_params) {
  double value;
  if (base::StringToDouble(
          GetVariationParam(quic_trial_params, "packet_loss_threshold"),
          &value)) {
    return static_cast<float>(value);
  }
  return 0.0f;
}

int GetQuicSocketReceiveBufferSize(
    const VariationParameters& quic_trial_params) {
  int value;
  if (base::StringToInt(
          GetVariationParam(quic_trial_params, "receive_buffer_size"),
          &value)) {
    return value;
  }
  return 0;
}

bool ShouldQuicDelayTcpRace(const VariationParameters& quic_trial_params) {
  return !base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "disable_delay_tcp_race"), "true");
}

bool ShouldQuicCloseSessionsOnIpChange(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "close_sessions_on_ip_change"),
      "true");
}

int GetQuicIdleConnectionTimeoutSeconds(
    const VariationParameters& quic_trial_params) {
  int value;
  if (base::StringToInt(GetVariationParam(quic_trial_params,
                                          "idle_connection_timeout_seconds"),
                        &value)) {
    return value;
  }
  return 0;
}

bool ShouldQuicDisablePreConnectIfZeroRtt(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "disable_preconnect_if_0rtt"),
      "true");
}

std::unordered_set<std::string> GetQuicHostWhitelist(
    const base::CommandLine& command_line,
    const VariationParameters& quic_trial_params) {
  std::string whitelist;
  if (command_line.HasSwitch(switches::kQuicHostWhitelist)) {
    whitelist = command_line.GetSwitchValueASCII(switches::kQuicHostWhitelist);
  } else {
    whitelist = GetVariationParam(quic_trial_params, "quic_host_whitelist");
  }
  std::unordered_set<std::string> hosts;
  for (const std::string& host : base::SplitString(
           whitelist, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
    hosts.insert(host);
  }
  return hosts;
}

bool ShouldQuicMigrateSessionsOnNetworkChange(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params,
                        "migrate_sessions_on_network_change"),
      "true");
}

bool ShouldQuicMigrateSessionsEarly(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params, "migrate_sessions_early"), "true");
}

bool ShouldQuicAllowServerMigration(
    const VariationParameters& quic_trial_params) {
  return base::LowerCaseEqualsASCII(
      GetVariationParam(quic_trial_params,
                        "allow_server_migration"),
      "true");
}

size_t GetQuicMaxPacketLength(const base::CommandLine& command_line,
                              const VariationParameters& quic_trial_params) {
  if (command_line.HasSwitch(switches::kQuicMaxPacketLength)) {
    unsigned value;
    if (!base::StringToUint(
            command_line.GetSwitchValueASCII(switches::kQuicMaxPacketLength),
            &value)) {
      return 0;
    }
    return value;
  }

  unsigned value;
  if (base::StringToUint(
          GetVariationParam(quic_trial_params, "max_packet_length"), &value)) {
    return value;
  }
  return 0;
}

net::QuicVersion ParseQuicVersion(const std::string& quic_version) {
  net::QuicVersionVector supported_versions = net::QuicSupportedVersions();
  for (size_t i = 0; i < supported_versions.size(); ++i) {
    net::QuicVersion version = supported_versions[i];
    if (net::QuicVersionToString(version) == quic_version) {
      return version;
    }
  }

  return net::QUIC_VERSION_UNSUPPORTED;
}

net::QuicVersion GetQuicVersion(const base::CommandLine& command_line,
                                const VariationParameters& quic_trial_params) {
  if (command_line.HasSwitch(switches::kQuicVersion)) {
    return ParseQuicVersion(
        command_line.GetSwitchValueASCII(switches::kQuicVersion));
  }

  return ParseQuicVersion(GetVariationParam(quic_trial_params, "quic_version"));
}

void ConfigureQuicParams(const base::CommandLine& command_line,
                         base::StringPiece quic_trial_group,
                         const VariationParameters& quic_trial_params,
                         bool is_quic_allowed_by_policy,
                         const std::string& quic_user_agent_id,
                         net::HttpNetworkSession::Params* params) {
  params->enable_quic = ShouldEnableQuic(command_line, quic_trial_group,
                                         is_quic_allowed_by_policy);
  params->disable_quic_on_timeout_with_open_streams =
      ShouldDisableQuicWhenConnectionTimesOutWithOpenStreams(quic_trial_params);

  params->enable_quic_alternative_service_with_different_host =
      ShouldQuicEnableAlternativeServicesForDifferentHost(command_line,
                                                          quic_trial_params);

  if (params->enable_quic) {
    params->quic_always_require_handshake_confirmation =
        ShouldQuicAlwaysRequireHandshakeConfirmation(quic_trial_params);
    params->quic_disable_connection_pooling =
        ShouldQuicDisableConnectionPooling(quic_trial_params);
    int receive_buffer_size = GetQuicSocketReceiveBufferSize(quic_trial_params);
    if (receive_buffer_size != 0) {
      params->quic_socket_receive_buffer_size = receive_buffer_size;
    }
    params->quic_delay_tcp_race = ShouldQuicDelayTcpRace(quic_trial_params);
    float load_server_info_timeout_srtt_multiplier =
        GetQuicLoadServerInfoTimeoutSrttMultiplier(quic_trial_params);
    if (load_server_info_timeout_srtt_multiplier != 0) {
      params->quic_load_server_info_timeout_srtt_multiplier =
          load_server_info_timeout_srtt_multiplier;
    }
    params->quic_enable_connection_racing =
        ShouldQuicEnableConnectionRacing(quic_trial_params);
    params->quic_enable_non_blocking_io =
        ShouldQuicEnableNonBlockingIO(quic_trial_params);
    params->quic_disable_disk_cache =
        ShouldQuicDisableDiskCache(quic_trial_params);
    params->quic_prefer_aes = ShouldQuicPreferAes(quic_trial_params);
    params->quic_force_hol_blocking = ShouldForceHolBlocking(quic_trial_params);
    int max_number_of_lossy_connections =
        GetQuicMaxNumberOfLossyConnections(quic_trial_params);
    if (max_number_of_lossy_connections != 0) {
      params->quic_max_number_of_lossy_connections =
          max_number_of_lossy_connections;
    }
    float packet_loss_threshold = GetQuicPacketLossThreshold(quic_trial_params);
    if (packet_loss_threshold != 0)
      params->quic_packet_loss_threshold = packet_loss_threshold;
    params->enable_quic_port_selection =
        ShouldEnableQuicPortSelection(command_line);
    params->quic_connection_options =
        GetQuicConnectionOptions(command_line, quic_trial_params);
    params->quic_close_sessions_on_ip_change =
        ShouldQuicCloseSessionsOnIpChange(quic_trial_params);
    int idle_connection_timeout_seconds =
        GetQuicIdleConnectionTimeoutSeconds(quic_trial_params);
    if (idle_connection_timeout_seconds != 0) {
      params->quic_idle_connection_timeout_seconds =
          idle_connection_timeout_seconds;
    }
    params->quic_disable_preconnect_if_0rtt =
        ShouldQuicDisablePreConnectIfZeroRtt(quic_trial_params);
    params->quic_host_whitelist =
        GetQuicHostWhitelist(command_line, quic_trial_params);
    params->quic_migrate_sessions_on_network_change =
        ShouldQuicMigrateSessionsOnNetworkChange(quic_trial_params);
    params->quic_migrate_sessions_early =
        ShouldQuicMigrateSessionsEarly(quic_trial_params);
    params->quic_allow_server_migration =
        ShouldQuicAllowServerMigration(quic_trial_params);
  }

  size_t max_packet_length =
      GetQuicMaxPacketLength(command_line, quic_trial_params);
  if (max_packet_length != 0) {
    params->quic_max_packet_length = max_packet_length;
  }

  params->quic_user_agent_id = quic_user_agent_id;

  net::QuicVersion version = GetQuicVersion(command_line, quic_trial_params);
  if (version != net::QUIC_VERSION_UNSUPPORTED) {
    net::QuicVersionVector supported_versions;
    supported_versions.push_back(version);
    params->quic_supported_versions = supported_versions;
  }

  if (command_line.HasSwitch(switches::kOriginToForceQuicOn)) {
    std::string origins =
        command_line.GetSwitchValueASCII(switches::kOriginToForceQuicOn);
    for (const std::string& host_port : base::SplitString(
             origins, ",", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL)) {
      if (host_port == "*")
        params->origins_to_force_quic_on.insert(net::HostPortPair());
      net::HostPortPair quic_origin = net::HostPortPair::FromString(host_port);
      if (!quic_origin.IsEmpty())
        params->origins_to_force_quic_on.insert(quic_origin);
    }
  }
}

void ParseFieldTrialsAndCommandLineInternal(
    const base::CommandLine& command_line,
    bool is_quic_allowed_by_policy,
    const std::string& quic_user_agent_id,
    net::HttpNetworkSession::Params* params) {
  // Always fetch the field trial groups to ensure they are reported correctly.
  // The command line flags will be associated with a group that is reported so
  // long as trial is actually queried.

  std::string quic_trial_group =
      base::FieldTrialList::FindFullName(kQuicFieldTrialName);
  VariationParameters quic_trial_params;
  if (!variations::GetVariationParams(kQuicFieldTrialName, &quic_trial_params))
    quic_trial_params.clear();
  ConfigureQuicParams(command_line, quic_trial_group, quic_trial_params,
                      is_quic_allowed_by_policy, quic_user_agent_id, params);

  std::string http2_trial_group =
      base::FieldTrialList::FindFullName(kHttp2FieldTrialName);
  ConfigureHttp2Params(command_line, http2_trial_group, params);

  const std::string tfo_trial_group =
      base::FieldTrialList::FindFullName(kTCPFastOpenFieldTrialName);
  ConfigureTCPFastOpenParams(tfo_trial_group, params);
}

}  // anonymous namespace

namespace network_session_configurator {

void ParseFieldTrials(bool is_quic_allowed_by_policy,
                      const std::string& quic_user_agent_id,
                      net::HttpNetworkSession::Params* params) {
  const base::CommandLine command_line(base::CommandLine::NO_PROGRAM);
  ParseFieldTrialsAndCommandLineInternal(
      command_line, is_quic_allowed_by_policy, quic_user_agent_id, params);
}

void ParseFieldTrialsAndCommandLine(bool is_quic_allowed_by_policy,
                                    const std::string& quic_user_agent_id,
                                    net::HttpNetworkSession::Params* params) {
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  ParseFieldTrialsAndCommandLineInternal(
      command_line, is_quic_allowed_by_policy, quic_user_agent_id, params);
}

}  // namespace network_session_configurator
