#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/limits.h>
#include <linux/vfio.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>

static int vfio_cfd = -1;
static uint64_t _iova = 0;

/* Convert virtual address to IOVA.
 * IOVA Adresses are censecutive. This mapping is not saved anywhere, so this
 * might be a bad idea? */
static uint64_t get_iova(uint32_t size) {
	uint64_t ret = _iova;
	_iova += size;
	return ret;
}

/* returns zero on success or -1 else. works only as root and should thus be
 * executed before the real program.
 * This function is as of yet untested, so ... beyond here be dragons. Or
 * unicorns. But most probably dragons.*/
int bind_pci_device_to_vfio(char* pci_addr) {
	// unbind old driver
	char path[PATH_MAX];
	snprintf(path, PATH_MAX, "/sys/bus/pci/devices/%s/driver/unbind", pci_addr);
	int fd = open(path, O_WRONLY);
	if (fd == -1) {
		// no driver loaded
	} else {
		if (write(fd, pci_addr, strlen(pci_addr)) != (ssize_t) strlen(pci_addr)) {
			return -1;
		}
		if (close(fd) < 0) {
			return -1;
		}
	}
	// get vendor and device id
	snprintf(path, PATH_MAX, "/sys/bus/pci/devices/%s/%s", pci_addr, "config");
	fd = open(path, O_RDONLY);
	uint16_t vendor_id, device_id;
	pread(fd, &vendor_id, sizeof(vendor_id), 0);
	pread(fd, &device_id, sizeof(device_id), 2);
	if (close(fd) < 0) {
		return -1;
	}
	// bind vfio driver
	snprintf(path, PATH_MAX, "%d %d", vendor_id, device_id);
	fd = open("/sys/bus/pci/drivers/vfio-pci/", O_WRONLY);
	if (fd == -1) {
		// that thing can not be found...
	} else {
		if (write(fd, path, strlen(path)) != (ssize_t) strlen(path)) {
			// could not write enough bytes
			return -1;
		}
		if (close(fd) < 0) {
			return -1;
		}
	}
	// Well, the unicorns *did* save the day!
	return 0;
}

void vfio_enable_dma(int device_fd) {
	// write to the command register (offset 4) in the PCIe config space
	int command_register_offset = 4;
	// bit 2 is "bus master enable", see PCIe 3.0 specification section 7.5.1.1
	int bus_master_enable_bit = 2;
	// Get region info for config region
	struct vfio_region_info conf_reg = { .argsz = sizeof(conf_reg) };
	conf_reg.index = VFIO_PCI_CONFIG_REGION_INDEX;
	ioctl(device_fd, VFIO_DEVICE_GET_REGION_INFO, &conf_reg);
	uint16_t dma = 0;
	pread(device_fd, &dma, 2, conf_reg.offset + command_register_offset);
	dma |= 1 << bus_master_enable_bit;
	pwrite(device_fd, &dma, 2, conf_reg.offset + command_register_offset);
}

/* returns the devices file descriptor or -1 on error */
int vfio_init(char* pci_addr) {
	// find iommu group for the device
	// `readlink /sys/bus/pci/device/<segn:busn:devn.funcn>/iommu_group`
	char path[PATH_MAX], iommu_group_path[PATH_MAX];
	struct stat st;
	snprintf(path, sizeof(path), "/sys/bus/pci/devices/%s/", pci_addr);
	int ret = stat(path, &st);
	if(ret < 0){
		// No such device
		return -1;
	}
	strncat(path, "iommu_group", sizeof(path) - strlen(path) - 1);

	int len = readlink(path, iommu_group_path, sizeof(iommu_group_path));
	if(len <= 0){
		printf("failed to find the iommu_group for device '%s'", pci_addr);
		return -1;
	}

	iommu_group_path[len] = '\0'; // append 0x00 to the string to end it
	char* group_name = basename(iommu_group_path);
	int groupid;
	ret = sscanf(group_name, "%d", &groupid);
	if(ret != 1){
		printf("Failed to convert group id '%s' to int", group_name);
		return -1;
	}

	int firstsetup = 0; // Need to set up the container exactly once
	if (vfio_cfd == -1) {
		firstsetup = 1;
		// open vfio file to create new cfio conainer
		vfio_cfd = open("/dev/vfio/vfio", O_RDWR);
		if(vfio_cfd < 0){
			printf("Failed to open /dev/vfio/vfio");
			return -1;
		}

		// check if the container's API version is the same as the VFIO API's
		if (ioctl(vfio_cfd, VFIO_GET_API_VERSION) != VFIO_API_VERSION) {
			printf("unknown VFIO API Version");
			return -1;
		}

		// check if type1 is supported
		if (ioctl(vfio_cfd, VFIO_CHECK_EXTENSION, VFIO_TYPE1_IOMMU) != 1) {
			printf("container doesn't support Type1 IOMMU");
			return -1;
		}
	}

	// open VFIO group containing the device
	snprintf(path, sizeof(path), "/dev/vfio/%d", groupid);
	int vfio_gfd = open(path, O_RDWR);
	if(vfio_gfd < 0){
		printf("Failed to open vfio group");
		return -1;
	}

	// check if group is viable
	struct vfio_group_status group_status = { .argsz = sizeof(group_status) };
	ret = ioctl(vfio_gfd, VFIO_GROUP_GET_STATUS, &group_status);
	if(ret == -1) {
		printf("failed to get VFIO group status. errno: %d", errno);
		return ret;
	}
	if(!group_status.flags & VFIO_GROUP_FLAGS_VIABLE){
		printf("VFIO group is not viable - are all devices in the group bound to the VFIO driver?");
		return -1;
	}

	// Add group to container
	ret = ioctl(vfio_gfd, VFIO_GROUP_SET_CONTAINER, &vfio_cfd);
	if(ret == -1){
		printf("Failed to set container. errno: %d", errno);
		return -1;
	}

	if (firstsetup != 0) {
		// Set vfio type (type1 is for IOMMU like VT-d or AMD-Vi) for the
		// container.
		// This can only be done after at least one group is in the container.
		ret = ioctl(vfio_cfd, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU);
		if(ret != 0){
			printf("Failed to set iommu type. errno: %d", -ret);
			return -1;
		}
	}

	// get device file descriptor
	int vfio_fd = ioctl(vfio_gfd, VFIO_GROUP_GET_DEVICE_FD, pci_addr);
	if(vfio_fd < 0){
		printf("Cannot get device fd. errno: %d", -vfio_fd);
		return -1;
	}

	// enable DMA
	vfio_enable_dma(vfio_fd);

	return vfio_fd;
}

/* returns a uint8_t pointer to the MMAPED region or MAP_FAILED if failed */
uint8_t* vfio_map_region(int vfio_fd, int region_index) {
	struct vfio_region_info region_info = { .argsz = sizeof(region_info) };
	region_info.index = region_index;
	int ret = ioctl(vfio_fd, VFIO_DEVICE_GET_REGION_INFO, &region_info);
	if(ret == -1){
		// Failed to set iommu type
		return MAP_FAILED; // MAP_FAILED == ((void *) -1)
	}
	return (uint8_t*) mmap(NULL, region_info.size, PROT_READ | PROT_WRITE, MAP_SHARED, vfio_fd, region_info.offset);
}

/* returns iova (physical address of the DMA memory from device view) on success */
uint64_t vfio_map_dma(void *vaddr, uint32_t size) {
	// uint64_t iova = get_iova(size); // bad idea, see description of get_iova
	uint64_t iova = (uint64_t) vaddr; // Identity mapping makes more sense
	struct vfio_iommu_type1_dma_map dma_map = {
		.vaddr = (uint64_t) vaddr,
		.iova = iova,
		.size = size,
		.argsz = sizeof(dma_map),
		.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE};
	ioctl(vfio_cfd, VFIO_IOMMU_MAP_DMA, &dma_map);
	return iova;
}

/* unmaps previously mapped DMA region. returns 0 on success */
uint64_t vfio_unmap_dma(int fd, uint64_t iova, uint32_t size) {
	struct vfio_iommu_type1_dma_unmap  dma_unmap = {
		.argsz = sizeof(dma_unmap),
		.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
		.iova = iova,
		.size = size};
	int ret = ioctl(vfio_cfd, VFIO_IOMMU_UNMAP_DMA, &dma_unmap);
	if(ret == -1){
		// Failed to unmap DMA region
		return -1;
	}
	return ret;
}
