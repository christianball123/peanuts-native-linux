// Peanuts Native for Linux: Bionic's system properties, answered from the device tree / DMI (ro.product.*) and empty otherwise.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#define PROP_VALUE_MAX 92
#define PROP_NAME_MAX 32
int __system_property_get(const char *name, char *value);
#ifdef __cplusplus
}
#endif
