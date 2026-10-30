#pragma once

#include <QString>

// Which cloud provider this host reports, shared by the metadata model
// (CloudStatus.h), the account inventory (CloudFleetSource.h) and the billed
// spend query (CloudSpendStatus.h) so none of them has to include another.
enum class CloudProvider {
    None,
    Aws,
    Azure,
    Gcp,
    Oci,
};

QString cloudProviderName(CloudProvider provider);
QString cloudProviderLabel(CloudProvider provider);
