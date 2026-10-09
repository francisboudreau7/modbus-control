#include <modbus.h>

#include <cstdio>

int main()
{
    modbus_t *ctx = modbus_new_tcp("127.0.0.1", 1502);
    std::printf("modbus_test_server placeholder, libmodbus %s\n", LIBMODBUS_VERSION_STRING);
    if (ctx) {
        modbus_free(ctx);
    }
    return 0;
}