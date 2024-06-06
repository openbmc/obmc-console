#include <systemd/sd-bus.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static int block_on_dbus_name(struct sd_bus *bus, char *service_name)
{
	sd_bus_track *track = NULL;
	int r;
	printf("blocking on dbus name: %s \n", service_name);

	r = sd_bus_track_new(bus, &track, NULL, NULL);
	if (r < 0) {
		fprintf(stderr, "Failed to create a new bus tracker: %s\n",
			strerror(-r));
		goto finish;
	}

	const uint64_t sleep_millis = 100;
	const uint64_t sleep_limit_millis = 10000;
	uint64_t totalsleep_millis = 0;
	while (totalsleep_millis <= sleep_limit_millis) {
		r = sd_bus_track_add_name(track, service_name);
		if (r < 0) {
			usleep(1000 * sleep_millis);
			totalsleep_millis += sleep_millis;
			continue;
		}
		break;
	}

	if (totalsleep_millis >= sleep_limit_millis) {
		fprintf(stderr, "Timeout while waiting for dbus name %s\n",
			service_name);
		r = -1;
		goto finish;
	} else {
		printf("done blocking on dbus name: %s \n", service_name);
	}

	r = sd_bus_track_remove_name(track, service_name);
	assert(r > 0);

finish:
	sd_bus_track_unref(track);

	return r < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

int block_on_dbus_console_id(struct sd_bus *bus, char *console_id)
{
	char dbus_name[200];
	sprintf(dbus_name, "xyz.openbmc_project.Console.%s", console_id);

	return block_on_dbus_name(bus, dbus_name);
}
