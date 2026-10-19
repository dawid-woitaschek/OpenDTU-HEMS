#pragma once
#include <diagnostics/Breadcrumb.h>
namespace Diagnostics {
void restartRequested(RestartSource source);
void intentionalRestart();
}
