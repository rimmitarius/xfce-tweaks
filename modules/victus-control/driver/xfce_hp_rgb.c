// SPDX-License-Identifier: GPL-2.0-or-later
/* HP RGB companion: no fan/EC/MUX operations and no replacement of hp-wmi.
 * WMI framing follows Linux hp-wmi.c (Matthew Garrett, Anssi Hannula et al.)
 * and the GPL-2.0-or-later hp-omen-extra driver by Yunus Emre YILMAZ.
 * Keyboard packet layout references are recorded in the module README.
 */
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/wmi.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/unaligned.h>
#define HP_GUID "5FB7F034-2C63-45E9-BE91-3D44E2C707E4"
static DEFINE_MUTEX(lock);
static struct platform_device *device;
struct request {__le32 signature,command,query,size;u8 data[128];};
static int query(u32 command,u32 operation,u8*data,size_t in,size_t out) {
    struct request *r;struct acpi_buffer input,output={ACPI_ALLOCATE_BUFFER,NULL};
    union acpi_object *obj;acpi_status status;int ret=-EIO;u32 firmware;
    if(in>128||out>128)return -EINVAL;
    r=kzalloc(sizeof(*r),GFP_KERNEL);if(!r)return -ENOMEM;
    r->signature=cpu_to_le32(0x55434553);r->command=cpu_to_le32(command);r->query=cpu_to_le32(operation);r->size=cpu_to_le32(in);
    if(in)memcpy(r->data,data,in);input.length=sizeof(*r);input.pointer=r;
    status=wmi_evaluate_method(HP_GUID,0,out>4?3:out?2:1,&input,&output);
    if(ACPI_FAILURE(status))goto done;
    obj=output.pointer;
    if(!obj||obj->type!=ACPI_TYPE_BUFFER||!obj->buffer.pointer||obj->buffer.length<8+out)goto done;
    firmware=get_unaligned_le32(obj->buffer.pointer+4);
    if(firmware){pr_debug("xfce_hp_rgb: command %x/%x returned %u\n",command,operation,firmware);ret=-EOPNOTSUPP;goto done;}
    if(out)memcpy(data,obj->buffer.pointer+8,out);ret=0;
done:kfree(output.pointer);kfree(r);return ret;
}
static int table(u8 data[128]) {memset(data,0,128);return query(0x20009,2,data,128,128);}
static ssize_t color_table_show(struct device*d,struct device_attribute*a,char*buf) {
    u8 data[128];int ret,i,pos=0;mutex_lock(&lock);ret=table(data);mutex_unlock(&lock);if(ret)return ret;
    for(i=0;i<128;i++)pos+=sysfs_emit_at(buf,pos,"%02x",data[i]);return pos+sysfs_emit_at(buf,pos,"\n");
}
static ssize_t color_show(struct device*d,struct device_attribute*a,char*buf) {
    u8 data[128];int ret;mutex_lock(&lock);ret=table(data);mutex_unlock(&lock);if(ret)return ret;
    return sysfs_emit(buf,"%02X%02X%02X\n",data[25],data[26],data[27]);
}
static ssize_t color_store(struct device*d,struct device_attribute*a,const char*buf,size_t count) {
    u8 existing[128],payload[128]={0};unsigned int color;int ret,i;
    if(count!=6 && !(count==7 && buf[6]=='\n'))return -EINVAL;
    for(i=0;i<6;i++)if(!isxdigit(buf[i]))return -EINVAL;
    if(kstrtouint(buf,16,&color)||color>0xffffff)return -EINVAL;
    mutex_lock(&lock);ret=table(existing);if(ret)goto done;
    /* Preserve the firmware table header and other zones. 8BC2 returns tag 3,
     * so treating this byte as an asserted 1/4-zone count is incorrect here.
     * This is the established hp-omen-extra read/modify/write zone-0 layout. */
    memcpy(payload,existing,sizeof(payload));
    payload[25]=color>>16;payload[26]=color>>8;payload[27]=color;
    ret=query(0x20009,3,payload,128,0);
done:mutex_unlock(&lock);return ret?ret:count;
}
static ssize_t brightness_show(struct device*d,struct device_attribute*a,char*buf) {
    u8 data[4]={0};int ret;mutex_lock(&lock);ret=query(0x20009,4,data,4,4);mutex_unlock(&lock);if(ret)return ret;
    return sysfs_emit(buf,"%u\n",get_unaligned_le32(data)==0xe4?1:0);
}
static ssize_t brightness_store(struct device*d,struct device_attribute*a,const char*buf,size_t count) {
    unsigned int value;u8 data[4];int ret;
    if(kstrtouint(buf,10,&value)||value>1)return -EINVAL;
    put_unaligned_le32(value?0xe4:0x64,data);mutex_lock(&lock);ret=query(0x20009,5,data,4,0);mutex_unlock(&lock);return ret?ret:count;
}
static DEVICE_ATTR_RO(color_table);
static DEVICE_ATTR_RW(color);
static DEVICE_ATTR_RW(brightness);
static struct attribute *attrs[]={&dev_attr_color_table.attr,&dev_attr_color.attr,&dev_attr_brightness.attr,NULL};
static const struct attribute_group group={.attrs=attrs};
static int __init start(void) {
    int ret;u8 data[128];
    if(!dmi_match(DMI_SYS_VENDOR,"HP")||!dmi_match(DMI_BOARD_NAME,"8BC2")||!wmi_has_guid(HP_GUID))return -ENODEV;
    /* Loading performs only a read; no lighting state is changed. */
    ret=table(data);if(ret)return ret;
    if(!data[0] || data[0]>8)return -EOPNOTSUPP;
    device=platform_device_register_simple("xfce-hp-rgb",-1,NULL,0);if(IS_ERR(device))return PTR_ERR(device);
    ret=sysfs_create_group(&device->dev.kobj,&group);if(ret){platform_device_unregister(device);return ret;}
    pr_info("xfce_hp_rgb: HP 8BC2 keyboard color-table header %u\n",data[0]);return 0;
}
static void __exit stop(void){sysfs_remove_group(&device->dev.kobj,&group);platform_device_unregister(device);}
module_init(start);module_exit(stop);
MODULE_LICENSE("GPL");MODULE_DESCRIPTION("XFCE Tweaks HP 8BC2 keyboard RGB companion");MODULE_AUTHOR("XFCE Tweaks contributors");
