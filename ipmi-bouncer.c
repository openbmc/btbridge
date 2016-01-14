#include <errno.h>
#include <stdio.h>

#include <systemd/sd-bus.h>

#define PREFIX "[IPMI] "

#define MSG_OUT(f_, ...) do { printf(PREFIX); printf((f_), ##__VA_ARGS__); } while(0)
#define MSG_ERR(f_, ...) do { fprintf(stderr,PREFIX); fprintf(stderr, (f_), ##__VA_ARGS__); } while(0)

sd_bus *bus;

static int bttest_ipmi(sd_bus_message *req,
		void *user_data, sd_bus_error *ret_error)
{
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL, *m=NULL;
    const char *dest, *path;
    int r, pty;
	unsigned char seq, netfn, lun, cmd;
	uint8_t buf[1];

	MSG_OUT("Got DBUS message\n");

	r = sd_bus_message_read(req, "yyyy",  &seq, &netfn, &lun, &cmd);
	if (r < 0) {
		MSG_ERR("FAIL ");
		errno = -r;
		perror("Couldn't read DBUS message");
		return -1;
	}

	dest = sd_bus_message_get_sender(req);
	path = sd_bus_message_get_path(req);

	r = sd_bus_message_new_method_call(bus, &m, dest, path,
			"org.openbmc.HostIpmi", "sendMessage");
	if (r < 0) {
		MSG_ERR("FAIL ");
		errno = -r;
		perror("Failed to add the method object");
		return -1;
	}

	/* Send CMD twice */
	r = sd_bus_message_append(m, "yyyyy", seq, netfn, lun, cmd, cmd);
	if (r < 0) {
		MSG_ERR("FAIL ");
		errno = -r;
		perror("Failed add the netfn and others");
		return -1;
	}

	r = sd_bus_message_append_array(m, 'y', buf, 1);
	if (r < 0) {
		MSG_ERR("FAIL ");
		errno = -r;
		perror("Failed to add the string of response bytes");
		return -1;
	}

	r = sd_bus_call(bus, m, 0, &error, &reply);
	if (r < 0) {
		MSG_ERR("FAIL ");
		errno = -r;
		perror("Failed to call the method");
		return -1;
	}

	r = sd_bus_message_read(reply, "x", &pty);
	if (r < 0) {
		MSG_ERR("FAIL ");
		errno = -r;
		perror("Failed to get a rc from the method");
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(m);

	return 0;
}

int main(int argc, char *argv[])
{
	sd_bus_slot *slot;
	int r;

	/* Connect to system bus */
	r = sd_bus_open_system(&bus);
	if (r < 0) {
		MSG_ERR("FAIL");
		errno = -r;
		perror("Failed to connect to system bus");
		return 1;
	}

	r = sd_bus_add_match(bus, &slot, "type='signal',"
			"interface='org.openbmc.HostIpmi',"
			"member='ReceivedMessage'", bttest_ipmi, NULL);
	if (r < 0) {
		MSG_ERR("FAIL");
		errno = -r;
		perror("Failed: sd_bus_add_match");
		goto finish;
	}


	for (;;) {
		r = sd_bus_process(bus, NULL);
		if (r < 0) {
			MSG_ERR("FAIL");
			errno = -r;
			perror("Failed to process bus");
			goto finish;
		}

		r = sd_bus_wait(bus, (uint64_t) - 1);
		if (r < 0) {
			MSG_ERR("FAIL");
			errno = -r;
			perror("Failed to wait on bus");
			goto finish;
		}
	}

finish:
	sd_bus_slot_unref(slot);
	sd_bus_unref(bus);

	return 0;
}
