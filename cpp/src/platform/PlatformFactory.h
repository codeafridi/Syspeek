#pragma once

#include "SystemBackend.h"

#include <QString>

#include <memory>

std::unique_ptr<SystemBackend> createSystemBackend();

SystemBackend &systemBackend();

QString platformName();

PlatformCapabilities backendCapabilities();
