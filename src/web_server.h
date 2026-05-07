#ifndef WEB_SERVER_H
#define WEB_SERVER_H

/**
 * @file web_server.h
 * @brief AsyncWebServer and WebSocket declarations
 * 
 * Provides:
 * - WiFi Access Point
 * - Web interface for configuration
 * - WebSocket for real-time data push
 * - REST API endpoints for counter/timer control
 */

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

// External references (defined in web_server.cpp)
extern AsyncWebServer server;
extern AsyncWebSocket ws;

/**
 * @brief Initialize web server and WiFi AP
 */
void web_server_init();

/**
 * @brief Send real-time data to all WebSocket clients
 */
void sendRealtimeData();

/**
 * @brief Get current counter value as String
 */
String get_counter_value();

/**
 * @brief Get current frequency as String
 */
String get_counter_frequency();

#endif // WEB_SERVER_H
