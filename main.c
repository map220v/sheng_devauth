#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>
#include <unistd.h>
#include "MinkCom.h"
#include "IClientEnv.h"
#include "IQSEEComCompatAppLoader.h"

#define DIST_NAME_SIZE 256

//RPMB Listner
extern int init(void);
extern void deinit(void);

static int qseecom_start_tzapp(
	Object app_loader,
	const char *file_path,
	const char *app_name,
	Object *app_controller
) {
	char dist_name[DIST_NAME_SIZE];
	size_t dist_name_len;
	size_t file_size;
	char *buffer;
	FILE *file;
	int ret;

	char file_name[1024] = { 0 };
	snprintf(file_name, sizeof(file_name), "%s/%s.mbn", file_path, app_name);

	file = fopen(file_name, "r");
	if (!file) {
		syslog(LOG_ERR, "File %s open error: %s\n", file_name, strerror(errno));
		return -1;
	}

	fseek(file, 0, SEEK_END);
	file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	buffer = malloc(file_size);
	fread(buffer, 1, file_size, file);
	fclose(file);

	ret = IQSEEComCompatAppLoader_loadFromBuffer(
		app_loader, buffer, file_size,
		app_name, sizeof(app_name),
		dist_name, DIST_NAME_SIZE, &dist_name_len,
		app_controller);
	if (ret) {
		syslog(LOG_ERR, "Loading tzapp failed, result %d\n", ret);
	}

	free(buffer);
	return ret;
}

void byte2string(const unsigned char *bytes, size_t len, char *output) {
    for (size_t i = 0; i < len; i++) {
        sprintf(output + i * 2, "%02X", bytes[i]);
    }
}

struct nanosic_auth_req {
    uint8_t uid[16];
    uint8_t challenge[16];
};

int main() {
	Object root_env = Object_NULL;
	Object client_env = Object_NULL;
	int ret;

	ret = MinkCom_getRootEnvObject(&root_env);
	if (ret) {
		syslog(LOG_ERR, "Failed to get TEE Root object, err=%d\n", ret);
		goto exit;
	}

	ret = MinkCom_getClientEnvObject(root_env, &client_env);
	if (ret) {
		syslog(LOG_ERR, "Failed to get TEE Client object, err=%d\n", ret);
		goto exit;
	}

	Object app_loader = Object_NULL;
	Object app_controller = Object_NULL;

	ret = IClientEnv_open(client_env, 122, &app_loader);
	if (ret) {
		syslog(LOG_ERR, "Failed to get QSEECOM App Loader object, err=%d\n", ret);
		goto exit;
	}

	ret = qseecom_start_tzapp(app_loader, "/usr/lib/firmware/qcom/sm8550/sheng", "devauth", &app_controller);
	if (ret) {
		goto exit;
	}

	int fd = open("/dev/nanosic_auth", O_RDWR);
	if (fd < 0) {
		ret = fd;
		syslog(LOG_ERR, "Can't open /dev/nanosic_auth, err=%d\n", ret);
		goto exit;
	}

	char *buffer_send = malloc(128);
	char *buffer_recv = malloc(128);
	size_t send_len;
	size_t recv_len;

	char uid[] = "00000000000000000000000000000000";
	char keymeta[] = "00000002";
	struct nanosic_auth_req req;
	while (1) {
		syslog(LOG_DEBUG, "Start wait for uid and keyboard challenge.\n");
		ssize_t n = read(fd, &req, sizeof(req));
		if (n != sizeof(req)) {
			syslog(LOG_ERR, "Wrong size of kernel request, expected=%lu got=%zd\n", sizeof(req), n);
			break;
		}
		syslog(LOG_DEBUG, "Got UID and keyboard challenge!\n");
		byte2string(req.uid, 16, uid);
		syslog(LOG_DEBUG, "UID=%s\n", uid);
		ret = init(); //rpmb listener
		if (ret) {
			syslog(LOG_ERR, "Failed to init RPMB listener, err=%d\n", ret);
			break;
		}
		memset(buffer_send, 0, 128);
		memset(buffer_recv, 0, 128);
		*(uint64_t *)(buffer_send + 0) = 0x1;
		*(uint32_t *)(buffer_send + 4) = 0x2020;
		memcpy(buffer_send + 12, uid, 32);
		memcpy(buffer_send + 12 + 32, keymeta, 8);
		memcpy(buffer_send + 12 + 40, req.challenge, 16);
		//devauth_OfflineToken_get - uses 16 random bytes from kb and 
		//offline key to produce offline pad token
		ret = IQSEEComCompat_sendRequest(
			app_controller,
			buffer_send, 128,
			buffer_recv, 128,
			buffer_send, 128, &send_len,
			buffer_recv, 128, &recv_len,
			NULL, 0,
			1,
			Object_NULL, Object_NULL,
			Object_NULL, Object_NULL
		);
		deinit(); //rpmb listner
		if (ret || *(uint32_t *)buffer_recv) {
			//it's fine without fs listner, we don't need to write UID to persist.
			if (*(uint32_t *)buffer_recv != 0xaa00000d) {
				syslog(LOG_ERR, "devauth command error %d status 0x%x\n", ret, *(uint32_t *)buffer_recv);
			}
		}
		n = write(fd, buffer_recv + 8, 16);
		if (n != 16) {
			syslog(LOG_ERR, "Wrong size of kernel response, expected=%d got=%zd\n", 16, n);
			break;
		}
		syslog(LOG_DEBUG, "Sent pad token to kernel driver!\n");
	}
	free(buffer_send);
	free(buffer_recv);
	close(fd);

exit:
	Object_RELEASE_IF(app_controller);
	Object_RELEASE_IF(app_loader);
	Object_RELEASE_IF(client_env);
	Object_RELEASE_IF(root_env);
	return ret;
}
