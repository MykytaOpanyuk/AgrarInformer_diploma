// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for handling externally connected button and LED.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/pm.h>

#include "diode_button.h"

#define DRIVER_NAME             "diode-button driver"

typedef struct diode_button {
        struct miscdevice       mdev;
        struct gpio_desc *      btn_gpio;
        struct gpio_desc *      led_gpio;
        int                     btn_irq;
        int                     led_on;
        int                     btn_on;
        int                     control; /* kernel controls LED switching? */
        spinlock_t              lock;
        wait_queue_head_t       wait;
        bool                    data_ready; /* new data ready to read */
} diode_button_st_t;

static inline diode_button_st_t *to_diode_button_struct(struct file *file)
{
	struct miscdevice *miscdev = file->private_data;

	return container_of(miscdev, diode_button_st_t, mdev);
}

static ssize_t diode_button_read(struct file *file, char __user *buf,
        size_t count, loff_t *ppos)
{
        diode_button_st_t *diode_button = to_diode_button_struct(file);
        unsigned long flags = 0;
        unsigned long copy_len = 0;
        const char *val;

        spin_lock_irqsave(&diode_button->lock, flags);
        while (!diode_button->data_ready) {
                spin_unlock_irqrestore(&diode_button->lock, flags);
                if (file->f_flags & O_NONBLOCK)
                        return -EAGAIN;
                if (wait_event_interruptible(diode_button->wait,
                                diode_button->data_ready))
                        return -ERESTARTSYS;
                spin_lock_irqsave(&diode_button->lock, flags);
        }

        val = diode_button->btn_on ? "1" : "0";
        diode_button->data_ready = false;
        spin_unlock_irqrestore(&diode_button->lock, flags);

        /* Handle case when user requested 1 byte read */
        copy_len = min(count, (size_t)READ_BUF_LEN);

        /* Do not advance ppos, do not use simple_read_from_buffer() */
        if (copy_to_user(buf, val, copy_len))
                return -EFAULT;

        return copy_len;
}

static ssize_t diode_button_write(struct file *file, const char __user *buf,
        size_t count, loff_t *ppos)
{
	struct diode_button *diode_button = to_diode_button_struct(file);
	unsigned long flags = 0;
	char kbuf[WRITE_BUF_LEN];
	long val = 0;

	/* Do not advance ppos, do not use simple_write_to_buffer() */
	if (copy_from_user(kbuf, buf, WRITE_BUF_LEN))
		return -EFAULT;

	kbuf[1] = '\0'; /* get rid of possible \n from "echo" command */

	if (kstrtol(kbuf, 0, &val))
		return -EINVAL;
	val = !!val;

	spin_lock_irqsave(&diode_button->lock, flags);
	if (diode_button->led_on != val) {
		diode_button->led_on = val;
		gpiod_set_value(diode_button->led_gpio, diode_button->led_on);
	}
	spin_unlock_irqrestore(&diode_button->lock, flags);

	return count;
}

static __poll_t diode_button_poll(struct file *file, poll_table *wait)
{
	struct diode_button *diode_button = to_diode_button_struct(file);
	unsigned long flags;
	__poll_t mask = 0;

	poll_wait(file, &diode_button->wait, wait);

	spin_lock_irqsave(&diode_button->lock, flags);
	if (diode_button->data_ready)
		mask = EPOLLIN | EPOLLRDNORM;
	spin_unlock_irqrestore(&diode_button->lock, flags);

	return mask;
}

static long diode_button_ioctl(struct file *file, unsigned int cmd,
        unsigned long arg)
{
	struct diode_button *diode_button = to_diode_button_struct(file);
	unsigned long flags;
	int val;

	switch (cmd) {
	case DIODE_BUTTON_IOC_SET_LED:
		if (get_user(val, (int __user *)arg))
			return -EFAULT;

		spin_lock_irqsave(&diode_button->lock, flags);
		diode_button->led_on = !!val;
		gpiod_set_value(diode_button->led_gpio, diode_button->led_on);
		spin_unlock_irqrestore(&diode_button->lock, flags);

		/* Fall through */
	case DIODE_BUTTON_IOC_GET_LED:
		spin_lock_irqsave(&diode_button->lock, flags);
		val = diode_button->led_on;
		spin_unlock_irqrestore(&diode_button->lock, flags);

		return put_user(val, (int __user *)arg);
	case DIODE_BUTTON_IOC_KERN_CONTROL:
		if (get_user(val, (int __user *)arg))
			return -EFAULT;

		spin_lock_irqsave(&diode_button->lock, flags);
		diode_button->control = !!val;
		spin_unlock_irqrestore(&diode_button->lock, flags);

		break;
	default:
		return -ENOTTY;
	}

	return 0;
}

static const struct file_operations diode_button_fops = {
	.owner		= THIS_MODULE,
	.read		= diode_button_read,
	.write		= diode_button_write,
	.poll		= diode_button_poll,
	.unlocked_ioctl = diode_button_ioctl,
	.llseek		= no_llseek,
};

static irqreturn_t diode_button_btn_isr(int irq, void *data)
{
	struct diode_button *diode_button = data;
	unsigned long flags;

	dev_dbg(diode_button->mdev.this_device, "%s: Interrupt requested.\n",
	        __func__);

	spin_lock_irqsave(&diode_button->lock, flags);

	diode_button->data_ready = true;
	diode_button->btn_on = gpiod_get_value(diode_button->btn_gpio);

	if (diode_button->btn_on && diode_button->control) {
		diode_button->led_on ^= 0x1;
		gpiod_set_value(diode_button->led_gpio, diode_button->led_on);
	}
	spin_unlock_irqrestore(&diode_button->lock, flags);

	wake_up_interruptible(&diode_button->wait);

	return IRQ_HANDLED;
}

static int diode_button_probe(struct platform_device *pdev)
{
        struct device *dev = &pdev->dev;
	struct device_node *node = pdev->dev.of_node;
	struct diode_button *diode_button;
	unsigned int debounce;
	bool wakeup_source;
	int ret;

	dev_dbg(dev, "%s: Probe started.\n", __func__);

	diode_button = devm_kzalloc(&pdev->dev,
	                sizeof(*diode_button), GFP_KERNEL);
	if (!diode_button)
	        return -ENOMEM;

	diode_button->control = 1;

	/* "button-gpios" in dts */
	diode_button->btn_gpio = devm_gpiod_get(dev, "button", GPIOD_IN);
	if (IS_ERR(diode_button->btn_gpio))
	        return PTR_ERR(diode_button->btn_gpio);

	/* "led-gpios" in dts */
	diode_button->led_gpio = devm_gpiod_get(dev, "led", GPIOD_OUT_LOW);
	if (IS_ERR(diode_button->led_gpio))
	        return PTR_ERR(diode_button->led_gpio);

	diode_button->btn_irq = gpiod_to_irq(diode_button->btn_gpio);
	if (diode_button->btn_irq < 0)
	        return diode_button->btn_irq;

	ret = of_property_read_u32(node, "debounce-delay-ms", &debounce);
	if (!ret) {
	        ret = gpiod_set_debounce(diode_button->btn_gpio, debounce * 1000);
		if (ret < 0)
		        dev_warn(dev, "No HW support for debouncing.\n");
	}

	wakeup_source = of_property_read_bool(node, "wakeup-source");

	ret = devm_request_irq(dev, diode_button->btn_irq, diode_button_btn_isr,
	                IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
	                dev_name(dev), diode_button);
	if (ret < 0)
	        return ret;

	device_init_wakeup(dev, wakeup_source);
	platform_set_drvdata(pdev, diode_button);
	spin_lock_init(&diode_button->lock);
	init_waitqueue_head(&diode_button->wait);

	diode_button->mdev.minor        = MISC_DYNAMIC_MINOR;
	diode_button->mdev.name	        = DRIVER_NAME;
	diode_button->mdev.fops	        = &diode_button_fops;
	diode_button->mdev.parent       = dev;

	ret = misc_register(&diode_button->mdev);
	if (ret)
	        return ret;

	gpiod_set_value(diode_button->led_gpio, 0);

	dev_dbg(dev, "%s: Probe finished OK.\n", __func__);

	return 0;
}

static int diode_button_remove(struct platform_device *pdev)
{
        struct diode_button *diode_button = platform_get_drvdata(pdev);

        misc_deregister(&diode_button->mdev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int diode_button_suspend(struct device *dev)
{
        struct diode_button *diode_button = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
	        enable_irq_wake(diode_button->btn_irq);

	return 0;
}

static int diode_button_resume(struct device *dev)
{
        struct diode_button *diode_button = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
	        disable_irq_wake(diode_button->btn_irq);

	return 0;
}
#endif /* CONFIG_PM_SLEEP */

static SIMPLE_DEV_PM_OPS(diode_button_pm, diode_button_suspend, diode_button_resume);

static const struct of_device_id diode_button_of_match[] = {
        { .compatible = "diode_button" },
        {},
};
MODULE_DEVICE_TABLE(of, diode_button_of_match);

static struct platform_driver diode_button_driver = {
        .probe = diode_button_probe,
	.remove = diode_button_remove,
	.driver = {
	        .name = DRIVER_NAME,
		.pm = &diode_button_pm,
		.of_match_table = diode_button_of_match,
	},
};

module_platform_driver(diode_button_driver);

MODULE_ALIAS("platform:diode_button");
MODULE_AUTHOR("Mykyta Opanyuk <mykyta@opanyuk.com>");
MODULE_DESCRIPTION("Diode button driver");
MODULE_LICENSE("GPL");
