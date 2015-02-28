/*
 * Copyright 2015
 *
 * Author: acdev <remittor@gmail.com>
 *
 * LED driver for the BCT3253 I2C LED driver 
 * 3ch Current Sink RGB LED driver for illumination
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/slab.h>


#ifdef CONFIG_OF
static struct of_device_id bct3253_of_match[] = {
  { .compatible = "bct,bct3253",},
  { },
};
#else
#define bct3253_of_match NULL
#endif

static const struct i2c_device_id bct3253_led_i2c_ids[] = {
  { "bct3253_led" },
  { }
};
MODULE_DEVICE_TABLE(i2c, bct3253_led_i2c_ids);

struct bct3253_data {
  int unk;
  int kk;
  spinlock_t led_lock;
  int blink_status[2];
  struct i2c_client *client;
  struct led_classdev led_cdev_blue;    // blue
  struct led_classdev led_cdev_bbl;     // button-backlight
};

struct tricolor_led_data {
  int blink_type;
  int status;
  int status2;
};

static struct tricolor_led_data  g_leds;

// ----------------------------------------------------------------------

static int bct3253_i2c_Write(struct i2c_client *client, char *writebuf, int writelen)
{
  struct i2c_msg msgs[1];
  int ret;
  msgs[0].addr = client->addr;
  msgs[0].flags = 0;
  msgs[0].len = writelen;
  msgs[0].buf = writebuf;
  ret = i2c_transfer(client->adapter, msgs, 1);
  if (ret < 0)
    dev_err(&client->dev, "%s: i2c write error.\n", __func__);  
  return ret;
} 

static int bct3253_write_reg(struct i2c_client *client, u8 addr, const u8 val)
{
  u8 buf[2];
  buf[0] = addr;
  buf[1] = val;
  return bct3253_i2c_Write(client, buf, sizeof(buf));
}

static void led_brightness_set_tricolor(struct led_classdev *led_cdev, enum led_brightness value)
{
  struct bct3253_data *bct; 
  unsigned long flags;
  int led_id;
  uint8_t reg_val;
  
  if (!led_cdev) {
    pr_err("%s: tricolor_led is NULL \n", __func__);
    return;
  }
  if (strcmp(led_cdev->name, "blue") == 0) {
    bct = container_of(led_cdev, struct bct3253_data, led_cdev_blue);
    led_id = 0;
  } else {
    bct = container_of(led_cdev, struct bct3253_data, led_cdev_bbl);
    led_id = 1;
  }

  spin_lock_irqsave(&bct->led_lock, flags);
  reg_val = 8;
  if (value != LED_OFF) {
    if (led_id == 0) {   // "blue"
      g_leds.status2 = 1;
      reg_val = 2;
    } 
  } else {    
    if (led_id == 0) {   // "blue"
      g_leds.status2 = 0;
      reg_val = 3;
    }
  }
  spin_unlock_irqrestore(&bct->led_lock, flags);
  
  if (reg_val == 2) {
    if (g_leds.status) {
      // Enable all buttons backlight (constant)
      bct3253_write_reg(bct->client, 0, 0x01); // chip RESET
      bct3253_write_reg(bct->client, 2, 0x40); // set overall brightness (max 0xE7)
      bct3253_write_reg(bct->client, 3, 0xE5); // set left and right buttons brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 4, 0xE5); // set center button brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 1, 0x03); // enable both LEDs (0b0000 0011)
    } else {
      // Enable center button backlight (constant)
      bct3253_write_reg(bct->client, 0, 0x01); // chip RESET
      bct3253_write_reg(bct->client, 4, 0xE5); // set center button brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 1, 0x02); // enable center LED (0b0000 0010)
    }
  } else 
  if (reg_val == 3) {
    if (g_leds.status) {
      // Enable all buttons backlight (constant)
      bct3253_write_reg(bct->client, 0, 0x01); // chip RESET
      bct3253_write_reg(bct->client, 2, 0x40); // set overall brightness (max 0xE7)
      bct3253_write_reg(bct->client, 3, 0xE5); // set left and right buttons brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 4, 0xE5); // set center button brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 1, 0x03); // enable both LEDs (0b0000 0011)
    } else {
      // Disable backlight completely
      bct3253_write_reg(bct->client, 0, 0x01); // chip RESET
    }
  } 
}

static void bct3253_brightness_set(struct led_classdev *led_cdev, enum led_brightness value)
{
  struct bct3253_data *bct;
  
  if (!led_cdev) {
    pr_err("%s: tricolor_led is NULL \n", __func__);
    return;
  }
  bct = container_of(led_cdev, struct bct3253_data, led_cdev_bbl);
  if (value != LED_OFF) {
    // Enable all buttons backlight (constant)
    bct3253_write_reg(bct->client, 0, 0x01); // chip RESET
    bct3253_write_reg(bct->client, 2, 0x40); // set overall brightness (max 0xE7)
    bct3253_write_reg(bct->client, 3, 0xE5); // set left and right buttons brightness (relative, max 0xFF)
    bct3253_write_reg(bct->client, 4, 0xE5); // set center button brightness (relative, max 0xFF)
    bct3253_write_reg(bct->client, 1, 0x03); // enable both LEDs (0b0000 0011)
    g_leds.status = 1;
    return;
  }
  
  if (g_leds.blink_type == 1) {
    // Enable center button with slow smooth BLINK mode
    bct3253_write_reg(bct->client, 0x00, 0x01); // chip RESET
    bct3253_write_reg(bct->client, 0x02, 0x40); // set overall brightness (max 0x9F in blink mode)
    bct3253_write_reg(bct->client, 0x04, 0xE5); // set center button brightness (relative, max 0xFF)
    bct3253_write_reg(bct->client, 0x07, 0x53); // set center button on/off time (0b0111 0011)
    bct3253_write_reg(bct->client, 0x0D, 0xF8); // set center button upper brightness limit (max 0xFF)
    bct3253_write_reg(bct->client, 0x0E, 0x00); // set center button lower brightness limit (max 0xFF)
    bct3253_write_reg(bct->client, 0x0F, 0x33); // set how smooth center button will raise brightness
    bct3253_write_reg(bct->client, 0x10, 0x33); // set how smooth center button will drop brightness
    bct3253_write_reg(bct->client, 0x01, 0x22); // enable center LED in blink mode (0b0010 0010)
  } else
  if (g_leds.blink_type == 2) {
    // Enable center button with slow on-off BLINK mode
    bct3253_write_reg(bct->client, 0x00, 0x01); // chip RESET
    bct3253_write_reg(bct->client, 0x02, 0x40); // set overall brightness (max 0x9F in blink mode)
    bct3253_write_reg(bct->client, 0x04, 0xE5); // set center button brightness (relative, max 0xFF)
    bct3253_write_reg(bct->client, 0x07, 0x24); // set center button on/off time (0b0010 0100)
    bct3253_write_reg(bct->client, 0x0D, 0xF8); // set center button upper brightness limit (max 0xFF)
    bct3253_write_reg(bct->client, 0x0E, 0x00); // set center button lower brightness limit (max 0xFF)
    bct3253_write_reg(bct->client, 0x0F, 0x00); // set how smooth center button will raise brightness
    bct3253_write_reg(bct->client, 0x10, 0x00); // set how smooth center button will drop brightness
    bct3253_write_reg(bct->client, 0x01, 0x22); // enable center LED in blink mode (0b0010 0010)
  } else
  if (g_leds.blink_type == 3) {
    // Enable center button with fast on-off BLINK mode
    bct3253_write_reg(bct->client, 0x00, 0x01); // chip RESET
    bct3253_write_reg(bct->client, 0x02, 0x40); // set overall brightness (max 0x9F in blink mode)
    bct3253_write_reg(bct->client, 0x04, 0xE5); // set center button brightness (relative, max 0xFF)
    bct3253_write_reg(bct->client, 0x07, 0x32); // set center button on/off time (0b0011 0010)
    bct3253_write_reg(bct->client, 0x0D, 0xF8); // set center button upper brightness limit (max 0xFF)
    bct3253_write_reg(bct->client, 0x0E, 0x00); // set center button lower brightness limit (max 0xFF)
    bct3253_write_reg(bct->client, 0x0F, 0x00); // set how smooth center button will raise brightness
    bct3253_write_reg(bct->client, 0x10, 0x00); // set how smooth center button will drop brightness
    bct3253_write_reg(bct->client, 0x01, 0x22); // enable center LED in blink mode (0b0010 0010)
  } else {
    if (g_leds.status2 == 1) {
      bct3253_write_reg(bct->client, 0, 0x01); // chip RESET
      bct3253_write_reg(bct->client, 4, 0xE5); // set center button brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 1, 0x02); // enable center LED (0b0000 0010)
    } else {
      bct3253_write_reg(bct->client, 0, 0x01); // chip RESET
    }
  }
  g_leds.status = 0;
}

// ----------------------------------------------------------------

static ssize_t led_blink_solid_store(struct device *dev,
             struct device_attribute *attr,
             const char *buf, size_t size)
{
  struct led_classdev *led_cdev;
  struct bct3253_data *bct;
  int led_id;
  unsigned long value;
  uint8_t reg_val;
  unsigned long flags;
  int ret;

  value = 0;
  led_cdev = dev_get_drvdata(dev);
  if (!led_cdev) {
    pr_err("%s: tricolor_led is NULL \n", __func__);
    return -EINVAL;
  }
  if (strcmp(led_cdev->name, "blue") == 0) {
    bct = container_of(led_cdev, struct bct3253_data, led_cdev_blue);
    led_id = 0;
  } else {
    bct = container_of(led_cdev, struct bct3253_data, led_cdev_bbl);
    led_id = 1;
  }

  //sscanf(buf, "%d", &value);
  ret = kstrtoul(buf, 10, &value);
  reg_val = 8;
  
  spin_lock_irqsave(&bct->led_lock, flags); 
  if (value == 1) {
    if (led_id == 0) {   // "blue"
      g_leds.blink_type = 1;
      reg_val = 4;
    }
  } else
  if (value == 2) {  
    if (led_id == 0) {   // "blue"
      g_leds.blink_type = 2;
      reg_val = 5;
    }
  } else
  if (value == 3) {  
    if (led_id == 0) {   // "blue"
      g_leds.blink_type = 3;
      reg_val = 6;
    }
  } else {
    if (led_id == 0) {   // "blue"
      g_leds.blink_type = 0;
      reg_val = 7;
    }
  }
  bct->blink_status[led_id] = value;
  spin_unlock_irqrestore(&bct->led_lock, flags);
  
  switch (reg_val) {
  case 4:
    // Enable center button with slow smooth BLINK mode
    bct3253_write_reg(bct->client, 0x00, 0x01); // chip RESET
    bct3253_write_reg(bct->client, 0x02, 0x40); // set overall brightness (max 0x9F in blink mode)
    bct3253_write_reg(bct->client, 0x04, 0xE5); // set center button brightness (relative, max 0xFF)
    bct3253_write_reg(bct->client, 0x07, 0x53); // set center button on/off time (0b0111 0011)
    bct3253_write_reg(bct->client, 0x0D, 0xF8); // set center button upper brightness limit (max 0xFF)
    bct3253_write_reg(bct->client, 0x0E, 0x00); // set center button lower brightness limit (max 0xFF)
    bct3253_write_reg(bct->client, 0x0F, 0x33); // set how smooth center button will raise brightness
    bct3253_write_reg(bct->client, 0x10, 0x33); // set how smooth center button will drop brightness
    bct3253_write_reg(bct->client, 0x01, 0x22); // enable center LED in blink mode (0b0010 0010)
    break;  
  case 5:
    if (g_leds.status) {
      // Enable all buttons backlight (constant)
      bct3253_write_reg(bct->client, 0, 0x01); // chip RESET
      bct3253_write_reg(bct->client, 2, 0x40); // set overall brightness (max 0xE7)
      bct3253_write_reg(bct->client, 3, 0xE5); // set left and right buttons brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 4, 0xE5); // set center button brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 1, 0x03); // enable both LEDs (0b0000 0011)
    } else {
      // Enable center button with slow on-off BLINK mode
      bct3253_write_reg(bct->client, 0x00, 0x01); // chip RESET
      bct3253_write_reg(bct->client, 0x02, 0x40); // set overall brightness (max 0x9F in blink mode)
      bct3253_write_reg(bct->client, 0x04, 0xE5); // set center button brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 0x07, 0x24); // set center button on/off time (0b0010 0100)
      bct3253_write_reg(bct->client, 0x0D, 0xF8); // set center button upper brightness limit (max 0xFF)
      bct3253_write_reg(bct->client, 0x0E, 0x00); // set center button lower brightness limit (max 0xFF)
      bct3253_write_reg(bct->client, 0x0F, 0x00); // set how smooth center button will raise brightness
      bct3253_write_reg(bct->client, 0x10, 0x00); // set how smooth center button will drop brightness
      bct3253_write_reg(bct->client, 0x01, 0x22); // enable center LED in blink mode (0b0010 0010)
    }
    break;  
  case 6:
    if (g_leds.status != 1) {
      // Enable center button with fast on-off BLINK mode
      bct3253_write_reg(bct->client, 0x00, 0x01); // chip RESET
      bct3253_write_reg(bct->client, 0x02, 0x40); // set overall brightness (max 0x9F in blink mode)
      bct3253_write_reg(bct->client, 0x04, 0xE5); // set center button brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 0x07, 0x32); // set center button on/off time (0b0011 0010)
      bct3253_write_reg(bct->client, 0x0D, 0xF8); // set center button upper brightness limit (max 0xFF)
      bct3253_write_reg(bct->client, 0x0E, 0x00); // set center button lower brightness limit (max 0xFF)
      bct3253_write_reg(bct->client, 0x0F, 0x00); // set how smooth center button will raise brightness
      bct3253_write_reg(bct->client, 0x10, 0x00); // set how smooth center button will drop brightness
      bct3253_write_reg(bct->client, 0x01, 0x22); // enable center LED in blink mode (0b0010 0010)
    } else {
      // Enable all buttons backlight (constant)
      bct3253_write_reg(bct->client, 0, 0x01); // chip RESET
      bct3253_write_reg(bct->client, 3, 0xE5); // set left and right buttons brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 4, 0xE5); // set center button brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 1, 0x03); // enable both LEDs (0b0000 0011)
    }
    break;  
  case 7:
    if (g_leds.status != 1) {
      bct3253_write_reg(bct->client, 0, 0x01); // chip RESET
    } else {
      bct3253_write_reg(bct->client, 0, 0x01); // chip RESET
      bct3253_write_reg(bct->client, 3, 0xE5); // set left and right buttons brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 4, 0xE5); // set center button brightness (relative, max 0xFF)
      bct3253_write_reg(bct->client, 1, 0x03); // enable both LEDs (0b0000 0011)
    }
    break;  
  }
  return size;
}

static ssize_t led_blink_solid_show(struct device *dev,
        struct device_attribute *attr,
        char *buf)
{
  struct led_classdev *led_cdev;
  struct bct3253_data *bct;
  int led_id;
  
  led_cdev = dev_get_drvdata(dev);
  if (!led_cdev) {
    pr_info("%s: tricolor_led is NULL \n", __func__);
    buf[0] = 0;
    return 0;
  }
  if (strcmp(led_cdev->name, "blue") == 0) {
    bct = container_of(led_cdev, struct bct3253_data, led_cdev_blue);
    led_id = 0;
  } else {
    bct = container_of(led_cdev, struct bct3253_data, led_cdev_bbl);
    led_id = 1;
  }
  return sprintf(buf, "%d\n", bct->blink_status[led_id]);
} 

// ----------------------------------------------------------------

static struct device_attribute dev_attr_blink_all = {
  .attr = {
     .name = "blink",
     .mode = 0644,
  },
  .show = led_blink_solid_show,
  .store = led_blink_solid_store,
};

// ----------------------------------------------------------------

static int __devinit tricolor_led_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
  struct bct3253_data *bct;
  int err;
  int r_blue = -1;
  int f_blue = -1;
  int r_bbl = -1;
  int f_bbl = -1;
  
  pr_info("%s\n", __func__);
  bct = kzalloc(sizeof(*bct), GFP_KERNEL);
  if (!bct) {
    dev_err(&client->dev, "no memory for device\n");
    return -ENOMEM;
  }
  memset(bct, 0, sizeof(*bct));
  spin_lock_init(&bct->led_lock);

  bct->kk = 0;
  bct->client = client;
  
  bct->led_cdev_blue.name = "blue";
  bct->led_cdev_blue.brightness_set = led_brightness_set_tricolor;

  bct->led_cdev_bbl.name = "button-backlight";
  bct->led_cdev_bbl.brightness_set = bct3253_brightness_set;
  
  err = r_blue = led_classdev_register(&client->dev, &bct->led_cdev_blue);
  if (err) {
    dev_err(&client->dev, "led_classdev_register '%s' failed (err = %d)\n", bct->led_cdev_blue.name, err);
    goto exit;
  } 
  err = r_bbl = led_classdev_register(&client->dev, &bct->led_cdev_bbl);
  if (err) {
    dev_err(&client->dev, "led_classdev_register '%s' failed (err = %d)\n", bct->led_cdev_bbl.name, err);
    goto exit;
  } 

  /* Add blink attributes */
  err = f_blue = device_create_file(bct->led_cdev_blue.dev, &dev_attr_blink_all);
  if (err) {
    dev_err(&client->dev, "failed to create blink attr for '%s'\n", bct->led_cdev_blue.name);
    goto exit;
  }
  err = f_bbl = device_create_file(bct->led_cdev_bbl.dev, &dev_attr_blink_all);
  if (err) {
    dev_err(&client->dev, "failed to create blink attr for '%s'\n", bct->led_cdev_bbl.name);
    goto exit;
  }
  
  i2c_set_clientdata(client, bct);
  pr_info("%s = OK \n", __func__);
  return 0;
  
exit:
  if (f_bbl == 0) 
    device_remove_file(bct->led_cdev_bbl.dev, &dev_attr_blink_all);
  if (f_blue == 0) 
    device_remove_file(bct->led_cdev_blue.dev, &dev_attr_blink_all);
  if (r_bbl == 0)
    led_classdev_unregister(&bct->led_cdev_bbl);
  if (r_blue == 0)
    led_classdev_unregister(&bct->led_cdev_blue);
  kfree(bct);
  return err;  
}

static int __devexit tricolor_led_remove(struct i2c_client *client)
{
  struct bct3253_data *bct = i2c_get_clientdata(client);

  pr_info("%s\n", __func__);
  device_remove_file(bct->led_cdev_bbl.dev, &dev_attr_blink_all);
  led_classdev_unregister(&bct->led_cdev_bbl);
  device_remove_file(bct->led_cdev_blue.dev, &dev_attr_blink_all);
  led_classdev_unregister(&bct->led_cdev_blue);
  kfree(bct);

  return 0;
}

// ----------------------------------------------------------------

static struct i2c_driver bct3253_led_i2c_driver = {
  .driver = {
      .name = "bct3253_led",
      .owner = THIS_MODULE,
      .of_match_table = bct3253_of_match,
  },
  .probe = tricolor_led_probe,
  .remove = __devexit_p(tricolor_led_remove),
  .id_table = bct3253_led_i2c_ids,
}; 
//module_i2c_driver(bct3253_led_i2c_driver);

static int __init tricolor_bct3253_led_init(void)
{
  pr_info("%s\n", __func__);
  return i2c_add_driver(&bct3253_led_i2c_driver);
}
module_init(tricolor_bct3253_led_init);

static void __exit tricolor_bct3253_led_exit(void)
{
  i2c_del_driver(&bct3253_led_i2c_driver);
}
module_exit(tricolor_bct3253_led_exit);

MODULE_AUTHOR("acdev <remittor@gmail.com>");
MODULE_DESCRIPTION("BCT3253 LED driver");
MODULE_LICENSE("GPL v2");
