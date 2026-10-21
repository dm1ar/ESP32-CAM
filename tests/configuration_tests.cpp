/*
 * ESP32-CAM raximax
 *
 * SPDX-FileCopyrightText: 2026 Axel Richter
 * SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
 */

#include "configuration.hpp"
#include "nvs.h"
#include <cassert>
#include <cstdio>
#include <string>
namespace {
std::string ssid{"test-network"}, password{"dummy-password"}, name{"ESP32-CAM02"};
std::uint32_t schema{1U}, minutes{20U};
std::uint8_t automatic{1U};
bool missing{false}, opened{false};
unsigned closes{0U};
}
esp_err_t nvs_flash_init_partition(const char* partition)
{
    assert(std::strcmp(partition,"config") == 0); return missing ? ESP_ERR_NOT_FOUND : ESP_OK;
}
esp_err_t nvs_open_from_partition(const char* partition,const char* space,int mode,nvs_handle_t* handle)
{
    assert(std::strcmp(partition,"config") == 0 && std::strcmp(space,"camera") == 0 && mode == NVS_READONLY);
    opened = true; *handle=1U; return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t,const char* key,char* value,std::size_t* size)
{
    const auto& source = std::strcmp(key,"ssid") == 0 ? ssid : std::strcmp(key,"password") == 0 ? password : name;
    if (*size < source.size()+1U) { return ESP_ERR_INVALID_SIZE; }
    std::memcpy(value,source.c_str(),source.size()+1U); *size=source.size()+1U; return ESP_OK;
}
esp_err_t nvs_get_u32(nvs_handle_t,const char* key,std::uint32_t* value)
{
    *value=std::strcmp(key,"schema") == 0 ? schema : minutes; return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t,const char*,std::uint8_t* value) { *value=automatic; return ESP_OK; }
void nvs_close(nvs_handle_t) { opened = false; ++closes; }
int main()
{
    cam::Configuration settings{};
    missing=true; assert(settings.load() == ESP_ERR_NOT_FOUND && closes == 0U); missing=false;
    assert(settings.load() == ESP_OK && !opened);
    assert(settings.ssid()==ssid && settings.password()==password && settings.nodeName()==name);
    assert(settings.policy().autoOff && settings.policy().limitSeconds==1200U);
    ssid=std::string(32U,'s'); password=std::string(64U,'a'); name=std::string(32U,'n');
    automatic=0U; minutes=120U;
    assert(settings.load()==ESP_OK && settings.ssid().size()==32U && settings.password().size()==64U);
    assert(!settings.policy().autoOff && settings.policy().limitSeconds==7200U);
    const auto reject=[&] { assert(settings.load()==ESP_ERR_NOT_FOUND && !opened); assert(settings.password().size()==64U); };
    schema=2U; reject(); schema=1U;
    minutes=4U; reject(); minutes=121U; reject(); minutes=20U;
    automatic=2U; reject(); automatic=1U;
    name="-invalid"; reject(); name="bad_name"; reject(); name="valid";
    ssid=""; reject(); ssid=std::string(33U,'s'); reject(); ssid=std::string{"a\0b",3U}; reject(); ssid="good";
    password="short"; reject(); password=std::string(64U,'z'); reject(); password=std::string(65U,'a'); reject();
    password=" leading trailing "; assert(settings.load()==ESP_OK && settings.password()==password);
    std::puts("PASS: NVS settings, schema/range/type bounds, exact strings, missing config and transactional failure cleanup");
}
