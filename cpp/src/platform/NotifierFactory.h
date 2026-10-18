#pragma once

#include "Notifier.h"

#include <memory>

std::unique_ptr<Notifier> createNotifier();
