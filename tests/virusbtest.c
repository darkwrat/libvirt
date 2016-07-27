/*
 * Copyright (C) 2013-2014 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Author: Jan Tomko <jtomko@redhat.com>
 */

#include <config.h>
#include <stdlib.h>

#include "viralloc.h"
#include "virerror.h"
#include "virfile.h"
#include "virusb.h"

#include "testutils.h"

#define VIR_FROM_THIS VIR_FROM_NONE

typedef enum {
    FIND_BY_ALL,
    FIND_BY_VENDOR,
    FIND_BY_DEVICE,
    FIND_BY_PORT
} testUSBFindFlags;

struct findTestInfo {
    const char *name;
    unsigned int vendor;
    unsigned int product;
    unsigned int bus;
    unsigned int devno;
    const char *port;
    const char *vroot;
    bool mandatory;
    int how;
    bool expectFailure;
};

static int testDeviceFileActor(virUSBDevicePtr dev,
                               const char *path,
                               void *opaque ATTRIBUTE_UNUSED)
{
    char *str = NULL;
    int ret = 0;

    if (virAsprintf(&str, USB_DEVFS "%03d/%03d",
                    virUSBDeviceGetBus(dev),
                    virUSBDeviceGetDevno(dev)) < 0)
        return -1;

    if (STRNEQ(path, str)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "Device path '%s' does not match expected '%s'",
                       path, str);
        ret = -1;
    }
    VIR_FREE(str);
    return ret;
}

static int testDeviceFind(const void *opaque)
{
    const struct findTestInfo *info = opaque;
    int ret = -1;
    virUSBDevicePtr dev = NULL;
    virUSBDeviceListPtr devs = NULL;
    int rv = 0;
    size_t i, ndevs = 0;
    unsigned int flags = 0;

    switch (info->how) {
    case FIND_BY_ALL:
        flags = USB_DEVICE_FIND_BY_VENDOR |
                USB_DEVICE_FIND_BY_DEVICE |
                USB_DEVICE_FIND_BY_PORT;
        break;
    case FIND_BY_VENDOR:
        flags = USB_DEVICE_FIND_BY_VENDOR;
        break;
    case FIND_BY_DEVICE:
        flags = USB_DEVICE_FIND_BY_DEVICE;
        break;
    case FIND_BY_PORT:
        flags = USB_DEVICE_FIND_BY_PORT;
        break;
    }

    rv = virUSBDeviceFind(info->vendor, info->product,
                          info->bus, info->devno, info->port,
                          info->vroot, info->mandatory, flags, &devs);

    if (info->expectFailure) {
        if (rv >= 0) {
            virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                           "unexpected success");
        } else {
            ret = 0;
        }
        goto cleanup;
    } else if (rv < 0) {
        goto cleanup;
    }

    if (info->how != FIND_BY_VENDOR) {
        if (rv == 1) {
            dev = virUSBDeviceListGet(devs, 0);
            virUSBDeviceListSteal(devs, dev);
        } else {
            goto cleanup;
        }
    }

    switch (info->how) {
    case FIND_BY_ALL:
    case FIND_BY_DEVICE:
        if (virUSBDeviceFileIterate(dev, testDeviceFileActor, NULL) < 0)
            goto cleanup;
        break;

    case FIND_BY_VENDOR:
        ndevs = virUSBDeviceListCount(devs);

        for (i = 0; i < ndevs; i++) {
            virUSBDevicePtr device = virUSBDeviceListGet(devs, i);
            if (virUSBDeviceFileIterate(device, testDeviceFileActor, NULL) < 0)
                goto cleanup;
        }
        break;
    }

    ret = 0;

 cleanup:
    virObjectUnref(devs);
    virUSBDeviceFree(dev);
    return ret;
}


static int
testCheckNdevs(const char* occasion,
               size_t got,
               size_t expected)
{
    if (got != expected) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "%s: got %zu devices, expected %zu",
                       occasion, got, expected);
        return -1;
    }
    return 0;
}


static int
testUSBList(const void *opaque ATTRIBUTE_UNUSED)
{
    virUSBDeviceListPtr list = NULL;
    virUSBDeviceListPtr devlist = NULL;
    virUSBDevicePtr dev = NULL;
    virUSBDeviceListPtr devs = NULL;
    int ret = -1;
    int rv;
    size_t i, ndevs;

    if (!(list = virUSBDeviceListNew()))
        goto cleanup;

#define EXPECTED_NDEVS_ONE 3
    if (virUSBDeviceFind(0x1d6b, 0x0002, 0, 0, NULL, NULL, true,
                         USB_DEVICE_FIND_BY_VENDOR, &devlist) < 0)
        goto cleanup;

    ndevs = virUSBDeviceListCount(devlist);
    if (testCheckNdevs("After first find", ndevs, EXPECTED_NDEVS_ONE) < 0)
        goto cleanup;

    for (i = 0; i < ndevs; i++) {
        dev = virUSBDeviceListGet(devlist, 0);
        dev = virUSBDeviceListSteal(devlist, dev);

        if (virUSBDeviceListAdd(list, dev) < 0)
            goto cleanup;
        dev = NULL;
    }

    virObjectUnref(devlist);
    devlist = NULL;

    ndevs = virUSBDeviceListCount(list);
    if (testCheckNdevs("After first loop", ndevs, EXPECTED_NDEVS_ONE) < 0)
        goto cleanup;

#define EXPECTED_NDEVS_TWO 3
    if (virUSBDeviceFind(0x18d1, 0x4e22, 0, 0, NULL, NULL, true,
                         USB_DEVICE_FIND_BY_VENDOR, &devlist) < 0)
        goto cleanup;

    ndevs = virUSBDeviceListCount(devlist);
    if (testCheckNdevs("After second find", ndevs, EXPECTED_NDEVS_TWO) < 0)
        goto cleanup;
    for (i = 0; i < ndevs; i++) {
        dev = virUSBDeviceListGet(devlist, 0);
        dev = virUSBDeviceListSteal(devlist, dev);

        if (virUSBDeviceListAdd(list, dev) < 0)
            goto cleanup;
        dev = NULL;
    }

    if (testCheckNdevs("After second loop",
                       virUSBDeviceListCount(list),
                       EXPECTED_NDEVS_ONE + EXPECTED_NDEVS_TWO) < 0)
        goto cleanup;

    rv = virUSBDeviceFind(0x18d1, 0x4e22, 1, 20, "1.5.6", NULL, true,
                          USB_DEVICE_FIND_BY_VENDOR |
                          USB_DEVICE_FIND_BY_DEVICE |
                          USB_DEVICE_FIND_BY_PORT, &devs);
    if (rv != 1) {
        goto cleanup;
    } else {
        dev = virUSBDeviceListGet(devs, 0);
        virUSBDeviceListSteal(devs, dev);
    }
    virObjectUnref(devs);

    if (!virUSBDeviceListFind(list, dev)) {
        virReportError(VIR_ERR_INTERNAL_ERROR,
                       "Device '%s' not in list when it should be",
                       virUSBDeviceGetName(dev));
        goto cleanup;
    }

    virUSBDeviceListDel(list, dev);
    dev = NULL;

    if (testCheckNdevs("After deleting one",
                       virUSBDeviceListCount(list),
                       EXPECTED_NDEVS_ONE + EXPECTED_NDEVS_TWO - 1) < 0)
        goto cleanup;

    ret = 0;

 cleanup:
    virObjectUnref(list);
    virObjectUnref(devlist);
    virUSBDeviceFree(dev);
    return ret;
}


static int
mymain(void)
{
    int rv = 0;

#define DO_TEST_FIND_FULL(name, vend, prod, bus, devno,                     \
                          port, vroot, mand, how, fail)                     \
    do {                                                                    \
        struct findTestInfo data = { name, vend, prod, bus,                 \
            devno, port, vroot, mand, how, fail                             \
        };                                                                  \
        if (virTestRun("USBDeviceFind " name, testDeviceFind, &data) < 0)   \
            rv = -1;                                                        \
    } while (0)

#define DO_TEST_FIND(name, vend, prod, bus, devno, port)                    \
    DO_TEST_FIND_FULL(name, vend, prod, bus, devno, port, NULL, true,       \
                      FIND_BY_ALL, false)
#define DO_TEST_FIND_FAIL(name, vend, prod, bus, devno, port)               \
    DO_TEST_FIND_FULL(name, vend, prod, bus, devno, port, NULL, true,       \
                      FIND_BY_ALL, true)

#define DO_TEST_FIND_BY_DEVICE(name, bus, devno)                               \
    DO_TEST_FIND_FULL(name, 101, 202, bus, devno, NULL, NULL, true,         \
                      FIND_BY_DEVICE, false)
#define DO_TEST_FIND_BY_DEVICE_FAIL(name, bus, devno)                          \
    DO_TEST_FIND_FULL(name, 101, 202, bus, devno, NULL, NULL, true,         \
                      FIND_BY_DEVICE, true)

#define DO_TEST_FIND_BY_VENDOR(name, vend, prod)                            \
    DO_TEST_FIND_FULL(name, vend, prod, 123, 456, NULL, NULL, true,         \
                      FIND_BY_VENDOR, false)
#define DO_TEST_FIND_BY_VENDOR_FAIL(name, vend, prod)                       \
    DO_TEST_FIND_FULL(name, vend, prod, 123, 456, NULL, NULL, true,         \
                      FIND_BY_VENDOR, true)

#define DO_TEST_FIND_BY_PORT(name, bus, port)                               \
    DO_TEST_FIND_FULL(name, 101, 202, bus, 456, port, NULL, true,           \
                      FIND_BY_PORT, false)
#define DO_TEST_FIND_BY_PORT_FAIL(name, bus, port)                          \
    DO_TEST_FIND_FULL(name, 101, 202, bus, 456, port, NULL, true,           \
                      FIND_BY_PORT, true)

    DO_TEST_FIND("Nexus", 0x18d1, 0x4e22, 1, 20, "1.5.6");
    DO_TEST_FIND_FAIL("Nexus wrong devnum", 0x18d1, 0x4e22, 1, 25, "1.5.6");
    DO_TEST_FIND_FAIL("Nexus wrong port", 0x18d1, 0x4e22, 1, 25, "1.5.4");
    DO_TEST_FIND_FAIL("Bogus", 0xf00d, 0xbeef, 1024, 768, "1.2.3.4");

    DO_TEST_FIND_BY_DEVICE("integrated camera", 1, 5);
    DO_TEST_FIND_BY_DEVICE_FAIL("wrong bus/devno combination", 2, 20);
    DO_TEST_FIND_BY_DEVICE_FAIL("missing bus", 5, 20);
    DO_TEST_FIND_BY_DEVICE_FAIL("missing devnum", 1, 158);

    DO_TEST_FIND_BY_VENDOR("Nexus (multiple results)", 0x18d1, 0x4e22);
    DO_TEST_FIND_BY_VENDOR_FAIL("Bogus vendor and product", 0xf00d, 0xbeef);
    DO_TEST_FIND_BY_VENDOR_FAIL("Valid vendor", 0x1d6b, 0xbeef);

    DO_TEST_FIND_BY_PORT("Logitech mouse", 1, "1.5.3.3");
    DO_TEST_FIND_BY_PORT_FAIL("wrong bus/port combination", 2, "1.5.3.3");
    DO_TEST_FIND_BY_PORT_FAIL("missing bus", 5, "1.5.3.3");
    DO_TEST_FIND_BY_PORT_FAIL("missing port", 1, "8.2.5");

    if (virTestRun("USB List test", testUSBList, NULL) < 0)
        rv = -1;

    if (rv < 0)
        return EXIT_FAILURE;
    return EXIT_SUCCESS;
}

VIRT_TEST_MAIN_PRELOAD(mymain, abs_builddir "/.libs/virusbmock.so")
