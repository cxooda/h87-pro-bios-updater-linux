#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>


#define FIRMWARE_NAME "firmware.bin"
#define FIRMWARE_CRC 0xe1c61e4f
#define FWU_MTU 512
#define CHUNK_SIZE (FWU_MTU - 12)
#define REPLY_TIMEOUT_MS 15000
#define INSTALL_TIMEOUT_S 300
#define MEI_CONNECT 0xC0104801UL /* IOCTL_MEI_CONNECT_CLIENT */

enum fwu_command {
    FWU_GET_VERSION = 0,
    FWU_GET_VERSION_REPLY = 1,
    FWU_START = 2,
    FWU_START_REPLY = 3,
    FWU_DATA = 4,
    FWU_DATA_REPLY = 5,
    FWU_END = 6,
    FWU_VERIFY_OEM_ID = 16,
    FWU_VERIFY_OEM_ID_REPLY = 17,
    FWU_GET_INFO = 26,
    FWU_GET_INFO_REPLY = 27
};

enum mkhi {
    MKHI_GROUP_UPDATE = 6,
    MKHI_GET_UPDATE_STATUS = 2,
    MKHI_RESPONSE = 0x80,
    MKHI_RESULT_ERROR = 2 /* 0 or 1 = valid status*/
};

enum update_status {
    UPDATE_SUCCESS = 0,
    UPDATE_IN_PROGRESS = 0x213
};

enum local_update_policy {
    LOCAL_UPDATE_DISABLED = 0,
    LOCAL_UPDATE_ENABLED = 1,
    LOCAL_UPDATE_PASSWORD_PROTECTED = 2
};

struct fwu_version {
    uint16_t major, minor, hotfix, build;
} __attribute__((packed));

struct fwu_header {
    uint32_t command;
    uint32_t status;
} __attribute__((packed));

struct fwu_version_reply {
    struct fwu_header header;
    uint32_t sku;
    uint32_t unknown_0c;
    uint32_t vendor;
    uint32_t last_update_status;
    uint32_t unknown_18;
    struct fwu_version code_version;
    struct fwu_version version_like_24;
    uint16_t local_update_policy;
    uint16_t unknown_2e;
    uint32_t data_format_version;
    uint32_t last_update_reset_type;
} __attribute__((packed));

struct fwu_partition_info {
    char name[4];
    uint32_t unknown_04;
    struct fwu_version version_like_08;
    uint32_t unknown_10[2];
    uint32_t manifest_data_id; /* $MCP + 0x40 */
    uint32_t data_format_version; /* $MCP + 0x3c */
    uint32_t unknown_20[4];
} __attribute__((packed));

struct fwu_info_reply {
    struct fwu_header header;
    uint32_t unknown_08[2];
    uint32_t unknown_10; /* 48? */
    uint32_t record_count;
    struct fwu_partition_info partitions[4];
    uint8_t unknown_d8[16];
} __attribute__((packed));

struct fwu_oem_id_request {
    uint32_t command;
    uint8_t oem_id[16];
} __attribute__((packed));

struct fwu_start_request {
    uint32_t command;
    uint32_t length;
    uint8_t unknown_08;
    uint8_t password_length;
    uint8_t password[32];
    uint32_t unknown_2a;
    uint32_t unknown_2e;
    uint32_t unknown_32;
    uint8_t oem_id[16];
    uint32_t unknown_46;
    uint8_t unknown_4a[12];
} __attribute__((packed));

struct fwu_start_reply {
    struct fwu_header header;
    uint8_t unknown_08[16];
} __attribute__((packed));

struct fwu_data_request {
    uint32_t command;
    uint32_t length;
    uint8_t unknown_08[3];
    uint8_t data[CHUNK_SIZE + 1];
} __attribute__((packed));

union fwu_reply {
    struct fwu_header header;
    struct fwu_version_reply version;
    struct fwu_info_reply info;
    struct fwu_start_reply start;
    uint8_t raw[FWU_MTU];
};

struct mkhi_status_reply {
    uint8_t group, command, unknown_02, result;
    uint32_t percent;
    uint32_t stage;
    uint32_t total_stages;
    uint32_t update_status;
    uint32_t reset_type;
    uint32_t unknown_18; /* unused */
} __attribute__((packed));

static const uint8_t FWU_UUID[16] = {
    0xe8, 0xcd, 0x9d, 0x30, 0xb1, 0xcc, 0x62, 0x40, 0x8f, 0x78, 0x60, 0x01, 0x15, 0xa3, 0x43, 0x27};
static const uint8_t MKHI_UUID[16] = {
    0x15, 0x67, 0x6a, 0x8e, 0xbc, 0x9a, 0x43, 0x40, 0x88, 0xef, 0x9e, 0x39, 0xc6, 0xf6, 0x3e, 0x0f};

static const struct fwu_version TARGET = {9, 0, 30, 1482};

static const struct fwu_version_reply SOURCE = {
    .header = {FWU_GET_VERSION_REPLY, 0},
    .sku = 0xc11199e0,
    .unknown_0c = 0x00800005,
    .vendor = 0x8086,
    .last_update_status = 0,
    .unknown_18 = 0x01001860,
    .code_version = {9, 0, 2, 1345},
    .version_like_24 = {9, 0, 2, 1345},
    .local_update_policy = LOCAL_UPDATE_ENABLED,
    .unknown_2e = 1,
    .data_format_version = 0x10000,
    .last_update_reset_type = 0,
};

static const struct fwu_info_reply SOURCE_INFO = {
    .header = {FWU_GET_INFO_REPLY, 0},
    .unknown_10 = 48,
    .record_count = 1,
    .partitions = {{
        .name = {'M', 'D', 'M', 'V'},
        .unknown_04 = 0x20040000,
        .version_like_08 = {9, 0, 2, 1345},
        .unknown_10 = {0x1234, 0x10000},
        .manifest_data_id = 0x1234,
        .data_format_version = 0x10000,
    }},
};

static int started;

static void panic(const char *msg)
{
    if (msg)
        fprintf(stderr, "error: %s\n", msg);
    if (started)
        fprintf(stderr, "Update failed mid run. Run bios-updater --status for info\n");
    exit(1);
}

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFF;
    size_t i;
    int b;

    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (b = 0; b < 8; b++)
            crc = crc >> 1 ^ (crc & 1 ? 0xEDB88320 : 0);
    }
    return ~crc;
}

static uint8_t *read_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data;
    long n;

    if (!f) {
        fprintf(stderr,"failed to open file \"%s\"\n", path);
        panic(0);
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    data = malloc(n);
    if (!data || fread(data, 1, n, f) != (size_t)n) {
        fprintf(stderr,"failed to read file \"%s\"\n", path);
        panic(0);
    }
    fclose(f);
    *size = (uint32_t)n;
    return data;
}

static int mei_connect(const uint8_t *uuid, uint32_t *mtu)
{
    uint8_t buf[16];
    int fd = open("/dev/mei0", O_RDWR);
    if (fd < 0)
        return -1;
    memcpy(buf, uuid, 16);
    if (ioctl(fd, MEI_CONNECT, buf) < 0) {
        close(fd);
        return -1;
    }
    memcpy(mtu, buf, 4);
    return fd;
}

static int send_message(int fd, const void *msg, size_t n)
{
    if (write(fd, msg, n) == (ssize_t)n && fsync(fd) == 0)
        return 0;  
    else
        return -1; 
}

static int request(int fd, const void *msg, size_t n, void *reply, size_t max)
{
    struct pollfd p = {fd, POLLIN, 0};
    if (send_message(fd, msg, n) < 0 || poll(&p, 1, REPLY_TIMEOUT_MS) != 1)
        return -1;
    return (int)read(fd, reply, max);
}

static void check_reply(const union fwu_reply *reply, int n, size_t size, enum fwu_command command)
{
    if (n != (int)size || reply->header.command != (uint32_t)command || reply->header.status) {
        fprintf(stderr, "expected reply %d: got %d bytes, command %lu, status 0x%lx\n", command, n,
                (unsigned long)reply->header.command, (unsigned long)reply->header.status);

        panic("Request rejected by ME");
    }
}

/* Returns result status or -1 if the status could not be read. */
static int get_update_status(struct mkhi_status_reply *status)
{
    static const uint8_t req[4] = {MKHI_GROUP_UPDATE, MKHI_GET_UPDATE_STATUS, 0, 0};
    uint32_t mtu;
    int fd = mei_connect(MKHI_UUID, &mtu), n;
    if (fd < 0)
        return -1;
    n = request(fd, req, sizeof req, status, sizeof *status);
    close(fd);
    if (n != sizeof *status || status->group != MKHI_GROUP_UPDATE ||
        status->command != (MKHI_GET_UPDATE_STATUS | MKHI_RESPONSE))
        return -1;
    return status->result;
}

static void print_status(const struct mkhi_status_reply *s)
{
    printf("result %u, %lu%%, stage %lu/%lu, update status 0x%lx, reset type %lu\n", s->result,
           (unsigned long)s->percent, (unsigned long)s->stage, (unsigned long)s->total_stages,
           (unsigned long)s->update_status, (unsigned long)s->reset_type);
}

int main(int argc, char **argv)
{
    static const char *dmi[2][2] = {{"/sys/class/dmi/id/board_name", "H87-PRO\n"},
                                    {"/sys/class/dmi/id/bios_version", "2104\n"}};
    const char *mode = argc > 1 ? argv[1] : "--check";
    const char *path = argc > 2 ? argv[2] : FIRMWARE_NAME;
    int flash, fd, n, r, i;
    uint32_t mtu, size, off, len, command;
    uint8_t *firmware;
    union fwu_reply reply;
    struct mkhi_status_reply status, last = {0};
    struct fwu_oem_id_request oem = {FWU_VERIFY_OEM_ID, {0}};
    struct fwu_start_request start = {.command = FWU_START};
    struct fwu_data_request data = {.command = FWU_DATA};
    const struct fwu_version *v = &reply.version.code_version;
    char line[64];
    FILE *f;

    flash = !strcmp(mode, "--flash");

    if (!flash && strcmp(mode, "--check") && strcmp(mode, "--status")) {
        fprintf(stderr, "usage: %s [--check | --flash | --status] [%s]\n", argv[0], FIRMWARE_NAME);
        return 2;
    }
    for (i = 0; i < 2; i++) {
        f = fopen(dmi[i][0], "r");
        if (!f || !fgets(line, sizeof line, f) || strcmp(line, dmi[i][1]))
            panic("Firmware mismatch detected. Flash BIOS 2104 with EZ Flash first.");
        fclose(f);
    }

    if (!strcmp(mode, "--status")) {
        if (get_update_status(&status) < 0)
            panic("Cannot read the ME update status");
        print_status(&status);
        return 0;
    }

    firmware = read_file(path, &size);
    if (crc32(firmware, size) != FIRMWARE_CRC) {
        fprintf(stderr, " %s is corrupted or not the right file\n", path);
        panic(0);
    }

    fd = mei_connect(FWU_UUID, &mtu);
    if (fd < 0)
        panic("Cannot connect to the ME firmware update client on /dev/mei0 (are you root?)");
    if (mtu != FWU_MTU)
        panic("Unexpected FWU message size");

    command = FWU_GET_VERSION;
    n = request(fd, &command, sizeof command, &reply, sizeof reply);
    check_reply(&reply, n, sizeof reply.version, FWU_GET_VERSION_REPLY);
    if (!memcmp(v, &TARGET, sizeof TARGET)) {
        printf("ME is already on 9.0.30.1482. Exiting.\n");
        return 0;
    }
    if (memcmp(&reply.version, &SOURCE, sizeof SOURCE)) {
        fprintf(stderr, "Detected ME %u.%u.%u.%u\n", v->major, v->minor, v->hotfix, v->build);
        panic("Only ME 9.0.2.1345 is supported, exiting");
    }

    command = FWU_GET_INFO;
    n = request(fd, &command, sizeof command, &reply, sizeof reply);
    check_reply(&reply, n, sizeof reply.info, FWU_GET_INFO_REPLY);
    if (memcmp(&reply.info, &SOURCE_INFO, sizeof SOURCE_INFO))
        panic("unexpected ME partition info");

    r = get_update_status(&status);
    if (r >= 0 && r != MKHI_RESULT_ERROR && status.update_status == UPDATE_IN_PROGRESS)
        panic("an ME update is already in progress!");
    if (!flash) {
        printf("Check validated, run --flash to update\n");
        return 0;
    }

    n = request(fd, &oem, sizeof oem, &reply, sizeof reply);
    check_reply(&reply, n, sizeof reply.header, FWU_VERIFY_OEM_ID_REPLY);


    /* prevent accidental Ctrl+C*/
    struct sigaction action = {0};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGPIPE, &action, NULL) < 0)
        panic("failed to install signal protection");

    started = 1;
    printf("Writing firmware. Do NOT turn off your machine or exit this program.\n");
    start.length = size;
    n = request(fd, &start, sizeof start, &reply, sizeof reply);
    check_reply(&reply, n, sizeof reply.start, FWU_START_REPLY);

    for (off = 0; off < size; off += len) {
        len = size - off < CHUNK_SIZE ? size - off : CHUNK_SIZE;
        data.length = len;
        memcpy(data.data, firmware + off, len);
        n = request(fd, &data, offsetof(struct fwu_data_request, data) + len + 1, &reply, sizeof reply);
        check_reply(&reply, n, sizeof reply.header, FWU_DATA_REPLY);
        if ((off + len) * 10 / size != off * 10 / size)
            printf("upload %lu%%\n", (unsigned long)(off + len) * 100 / size);
    }
    command = FWU_END;
    if (send_message(fd, &command, sizeof command) < 0)
        panic("sending END failed");
    close(fd);

    printf("Done writing firmware, installing now\n");
    for (i = 0; i < INSTALL_TIMEOUT_S; i++) {
        sleep(1);
        r = get_update_status(&status);
        if (r < 0 || r == MKHI_RESULT_ERROR)
            continue; /* client may drop while the ME resets */
        if (r > MKHI_RESULT_ERROR)
            panic("unexpected MKHI status result");
        if (memcmp(&status, &last, sizeof last)) {
            print_status(&status);
            last = status;
        }
        if (status.update_status != UPDATE_SUCCESS && status.update_status != UPDATE_IN_PROGRESS) {
            fprintf(stderr, "Installation failed with update status code %u\n", status.update_status);
            panic(0);
        }
        if (status.percent == 100 && status.update_status == UPDATE_SUCCESS) {
            printf("Installation complete. After rebooting, run --check if you want to verify the installation\n");
            return 0;
        }
    }
    panic("timeout");
    return 1;
}
