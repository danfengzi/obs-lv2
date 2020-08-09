/******************************************************************************
 *   Copyright (C) 2020 by Arkadiusz Hiler

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 2 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*****************************************************************************/

#include "worker.hpp"
#include <cassert>

#define OBS_LV2_RING_SIZE 4096

LV2Worker::LV2Worker(void)
{
	this->work_ring = zix_ring_new(OBS_LV2_RING_SIZE);
	this->response_ring = zix_ring_new(OBS_LV2_RING_SIZE);
}

LV2Worker::~LV2Worker()
{
	zix_ring_free(this->work_ring);
	zix_ring_free(this->response_ring);
}

static bool try_dequeue(ZixRing *ring, void *data, uint32_t size) {
	if (zix_ring_read_space(ring))
		return false;

	assert(zix_ring_read(ring, &size, sizeof(size)));
	assert(zix_ring_read(ring, data, size));

	return true;
}

static bool try_enqueue(ZixRing *ring, const void *data, uint32_t size) {
	if (zix_ring_write_space(ring) < (sizeof(size) + size))
		return false;

	assert(zix_ring_write(ring, &size, sizeof(size)));
	assert(zix_ring_write(ring, data, size));

	return true;
}

void *LV2Worker::process(void *arg) {
	void *data[OBS_LV2_RING_SIZE];
	uint32_t size = 0;
	bool canceled = false;

	LV2Worker *worker = (LV2Worker *) arg;

	while (!canceled) {
		if (!try_dequeue(worker->work_ring, data, size))
			continue;

		worker->plugin_interface->work(worker->plugin_instance,
					       LV2Worker::respond, worker,
					       size, data);

		/* TODO: we are busy-looping now, need some nice wait */
	}

	return NULL;
}

void LV2Worker::start(LV2_Handle instance, LV2_Worker_Interface *interface)
{
	this->plugin_instance = instance;
	this->plugin_interface = interface;

	/* TODO: make sure we start */
	pthread_create(&thread, NULL, LV2Worker::process, this);
}

void LV2Worker::stop()
{
	/* TODO: we want a better stopping mechanism */
	pthread_cancel(thread);
}

void LV2Worker::run(void)
{
	void *data[OBS_LV2_RING_SIZE];
	uint32_t size = 0;

	while (try_dequeue(this->response_ring, data, size))
		this->plugin_interface->work_response(this->plugin_instance,
						      size, data);

	if (this->plugin_interface->end_run != nullptr)
		this->plugin_interface->end_run(this->plugin_instance);
}


LV2_Worker_Status LV2Worker::schedule_work(LV2_Worker_Schedule_Handle handle,
					   uint32_t size, const void *data)
{
	LV2Worker *worker = (LV2Worker *) handle;

	if (try_enqueue(worker->work_ring, data, size))
		return LV2_WORKER_SUCCESS;
	else
		return LV2_WORKER_ERR_NO_SPACE;
}

LV2_Worker_Status LV2Worker::respond(LV2_Worker_Schedule_Handle handle,
				     uint32_t size, const void *data)
{
	LV2Worker *worker = (LV2Worker *) handle;

	if (try_enqueue(worker->response_ring, data, size))
		return LV2_WORKER_SUCCESS;
	else
		return LV2_WORKER_ERR_NO_SPACE;
}
