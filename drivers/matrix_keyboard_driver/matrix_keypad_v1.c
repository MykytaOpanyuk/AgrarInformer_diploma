#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/poll.h>
#include <linux/sched/signal.h>
#include <linux/wait.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/wait.h>

#define DRIVER_NAME             "matrix_keypad_v1"
#define MAX_DUMMY_PLAT_THREADS  1
#define QUEUE_DELAY_MS          15
#define READ_BUF_LEN            2

typedef struct matrix_keypad {
        struct                  miscdevice mdev;
        spinlock_t              lock;
        struct workqueue_struct *work_queue;
        wait_queue_head_t       wait;
        struct delayed_work     dwork;
        int                     *cols_state;
        int                     *irq_rows;
        char                    button_pushed;

        struct gpio_desc        **row_gpios;
        struct gpio_desc        **col_gpios;

        u32                     num_row_gpios;
        u32                     num_col_gpios;
        u32                     debounce_ms;
        u32                     col_scan_delay_us;

        bool                    scan_pending;
        bool                    stopped;
        bool                    wakeup;
        bool                    data_ready;
} matrix_keypad_st_t;

static void activate_col(const matrix_keypad_st_t *pdata,
                int col, bool on)
{
        if (on) {
                gpiod_direction_output(pdata->col_gpios[col], 0);
                udelay(pdata->col_scan_delay_us);
        } else {
                gpiod_direction_input(pdata->col_gpios[col]);
        }
}

static void activate_all_cols(const matrix_keypad_st_t *pdata,
                bool on)
{
        u32 col;

        for (col = 0; col < pdata->num_col_gpios; col++)
                activate_col(pdata, col, on);
}

static bool row_asserted(const matrix_keypad_st_t *pdata,
                int row)
{
        return gpiod_get_value_cansleep(pdata->row_gpios[row]) ?
                        0 : 1;
}

static char check_what_output(int x, int y) {
        char matrix_keys[4][4] = {
                        {'1','2','3','A'}, {'4','5','6','B'},
                        {'7','8','9','C'}, {'*','0','#','D'}
        };

        pr_info("%s: Pressed: %c\n", __func__, matrix_keys[x][y]);
        return matrix_keys[x][y];
}

static void matrix_keypad_scan(struct work_struct *work)
{
        matrix_keypad_st_t *keypad =
                container_of(work, matrix_keypad_st_t, dwork.work);
        int row, col;
        unsigned int i;
        int *new_cols_state =
                (int *)kzalloc(keypad->num_col_gpios * sizeof(int), GFP_KERNEL);

        activate_all_cols(keypad, 0);

        memset(new_cols_state, 0, sizeof(int) * keypad->num_col_gpios);

        for (col = 0; col < keypad->num_col_gpios; col++) {
                activate_col(keypad, col, 1);

                for (row = 0; row < keypad->num_row_gpios; row++)
                        new_cols_state[col] |=
                                row_asserted(keypad, row) ? (1 << row) : 0;

                activate_col(keypad, col, 0);
        }

        for (col = 0; col < keypad->num_col_gpios; col++) {
                u32 changing = new_cols_state[col] ^ keypad->cols_state[col];
                if (changing == 0)
                        continue;

                for (row = 0; row < keypad->num_row_gpios; row++) {
                        if ((changing & (1 << row)) == 0)
                                continue;
                        keypad->button_pushed = check_what_output(row, col);
                        keypad->data_ready = true;
                }
        }

        memcpy(keypad->cols_state, new_cols_state,
                        sizeof(int) * keypad->num_col_gpios);

        activate_all_cols(keypad, 1);

        spin_lock_irq(&keypad->lock);
        keypad->scan_pending = 0;

        for (i = 0; i < keypad->num_row_gpios; i++)
                enable_irq(keypad->irq_rows[i]);

        kfree(new_cols_state);

        spin_unlock_irq(&keypad->lock);
}

static irqreturn_t matrix_keypad_interrupt(int irq, void *id)
{
        matrix_keypad_st_t *keypad = id;
        unsigned long flags;
        unsigned int i;

        spin_lock_irqsave(&keypad->lock, flags);

        if (unlikely(keypad->scan_pending || keypad->stopped)) {
                spin_unlock_irqrestore(&keypad->lock, flags);
                return IRQ_HANDLED;
        }

        for (i = 0; i < keypad->num_row_gpios; i++)
                disable_irq_nosync(keypad->irq_rows[i]);
        keypad->scan_pending = 1;

        queue_delayed_work(keypad->work_queue, &keypad->dwork,
                        msecs_to_jiffies(QUEUE_DELAY_MS));

        spin_unlock_irqrestore(&keypad->lock, flags);
        return IRQ_HANDLED;
}

static inline matrix_keypad_st_t *to_matrix_keypad_struct(struct file *file)
{
        struct miscdevice *miscdev = file->private_data;

        return container_of(miscdev, matrix_keypad_st_t, mdev);
}

static ssize_t matrix_keypad_read(struct file *file, char __user *buf,
        size_t count, loff_t *ppos)
{
        matrix_keypad_st_t *matrix_keypad = to_matrix_keypad_struct(file);
        unsigned long flags = 0;
        unsigned long copy_len = 0;

        spin_lock_irqsave(&matrix_keypad->lock, flags);
        while (!matrix_keypad->data_ready) {
                spin_unlock_irqrestore(&matrix_keypad->lock, flags);
                if (file->f_flags & O_NONBLOCK)
                        return -EAGAIN;
                if (wait_event_interruptible(matrix_keypad->wait,
                                matrix_keypad->data_ready))
                        return -ERESTARTSYS;
                spin_lock_irqsave(&matrix_keypad->lock, flags);
        }

        matrix_keypad->data_ready = false;
        spin_unlock_irqrestore(&matrix_keypad->lock, flags);

        /* Handle case when user requested 1 byte read */
        copy_len = min(count, (size_t)READ_BUF_LEN);

        /* Do not advance ppos, do not use simple_read_from_buffer() */
        if (copy_to_user(buf, (void *)&matrix_keypad->button_pushed, copy_len))
                return -EFAULT;

        return copy_len;
}

static __poll_t matrix_keypad_poll(struct file *file, poll_table *wait)
{
        matrix_keypad_st_t *matrix_keypad = to_matrix_keypad_struct(file);
        unsigned long flags;
        __poll_t mask = 0;

        poll_wait(file, &matrix_keypad->wait, wait);

        spin_lock_irqsave(&matrix_keypad->lock, flags);
        if (matrix_keypad->data_ready)
                mask = EPOLLIN | EPOLLRDNORM;
        spin_unlock_irqrestore(&matrix_keypad->lock, flags);

        return mask;
}

static const struct file_operations matrix_keypad_fops = {
        .owner          = THIS_MODULE,
        .read           = matrix_keypad_read,
        .poll           = matrix_keypad_poll,
        .llseek         = no_llseek,
};

static int matrix_keypad_start(struct platform_device *dev)
{
        matrix_keypad_st_t *keypad = platform_get_drvdata(dev);

        keypad->stopped = 0;
        mb();

        queue_delayed_work(keypad->work_queue, &keypad->dwork, 0);

        return 0;
}

static void matrix_keypad_stop(struct platform_device *pdev)
{
        matrix_keypad_st_t *keypad = platform_get_drvdata(pdev);
        struct device *dev = &pdev->dev;
        unsigned int i;

        spin_lock_irq(&keypad->lock);
        keypad->stopped = 1;
        spin_unlock_irq(&keypad->lock);

        cancel_delayed_work_sync(&keypad->dwork);
        dev_info(dev,"%s: Delayed work cancelled!\n", __func__);

        for (i = 0; i < keypad->num_row_gpios; i++)
                disable_irq_nosync(keypad->irq_rows[i]);
        dev_info(dev, "%s: Disabled row_gpio lines interrupts!\n", __func__);
}

#ifdef CONFIG_PM_SLEEP
static void matrix_keypad_enable_wakeup(matrix_keypad_st_t *keypad)
{
        unsigned int i;

        for (i = 0; i < keypad->num_row_gpios; i++)
                enable_irq_wake(keypad->irq_rows[i]);
}

static void matrix_keypad_disable_wakeup(matrix_keypad_st_t *keypad)
{
        unsigned int i;

        for (i = 0; i < keypad->num_row_gpios; i++)
                disable_irq_wake(keypad->irq_rows[i]);
}

static int matrix_keypad_suspend(struct device *dev)
{
        matrix_keypad_st_t *keypad = dev_get_drvdata(dev);
        struct platform_device *pdev = to_platform_device(dev);

        matrix_keypad_stop(pdev);

        if (device_may_wakeup(dev))
                matrix_keypad_enable_wakeup(keypad);

        return 0;
}

static int matrix_keypad_resume(struct device *dev)
{
        struct platform_device *pdev = to_platform_device(dev);
        matrix_keypad_st_t *keypad = dev_get_drvdata(dev);

        if (device_may_wakeup(&pdev->dev))
                matrix_keypad_disable_wakeup(keypad);

        matrix_keypad_start(pdev);

        return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(matrix_keypad_pm,
                matrix_keypad_suspend, matrix_keypad_resume);

static int matrix_keypad_init_gpio(struct platform_device *pdev,
                matrix_keypad_st_t *keypad)
{
        u32 i, err = 0;
        struct device *dev = &pdev->dev;

        for (i = 0; i < keypad->num_row_gpios; i++) {
                err = gpiod_set_debounce(keypad->row_gpios[i],
                                keypad->debounce_ms);
                if (err < 0) {
                        dev_err(dev, "%s: No HW support for debouncing!\n",
                                     __func__);
                        dev_err(dev, "%s: Unable to acquire interrupt "
                                     "for GPIO line %i\n", __func__,
                                     gpiod_get_value(keypad->row_gpios[i]));
                        return 1;
                }
        }

        keypad->irq_rows = (int *)devm_kzalloc(dev,
                        keypad->num_row_gpios * sizeof(int), GFP_KERNEL);
        if (!keypad->irq_rows) {
                dev_err(dev, "%s: Could not allocate memory!\n", __func__);
                return -ENOMEM;
        }

        for (i = 0; i < keypad->num_row_gpios; i++) {
                keypad->irq_rows[i] = gpiod_to_irq(keypad->row_gpios[i]);
                if (keypad->irq_rows[i] < 0)
                        return 1;
        }

        for (i = 0; i < keypad->num_row_gpios; i++) {
                err = devm_request_irq(
                                dev, keypad->irq_rows[i],
                                matrix_keypad_interrupt,
                                IRQF_TRIGGER_FALLING |
                                IRQF_TRIGGER_HIGH |
                                IRQF_ONESHOT,
                                dev_name(dev), keypad);
                if (err < 0) {
                        dev_err(dev, "%s: devm request irq failed\n", __func__);
                        return 1;
                }
        }

        for (i = 0; i < keypad->num_row_gpios; i++)
                disable_irq_nosync(keypad->irq_rows[i]);

        return 0;
}

static matrix_keypad_st_t *matrix_keypad_parse_dt(struct device *dev)
{
        struct device_node *np = dev->of_node;
        struct gpio_desc *err = NULL;
        matrix_keypad_st_t *pdata;
        unsigned int i = 0;

        if (!np) {
                dev_err(dev, "%s: device lacks DT data!\n", __func__);
                return ERR_PTR(-ENODEV);
        }

        pdata = (matrix_keypad_st_t *)devm_kzalloc(dev,
                        sizeof(matrix_keypad_st_t), GFP_KERNEL);
        if (!pdata) {
                dev_err(dev, "%s: Could not allocate"
                                " memory for platform data!\n", __func__);
                return ERR_PTR(-ENOMEM);
        }

        pdata->num_row_gpios = pdata->num_col_gpios = 0;
        pdata->num_row_gpios = of_gpio_named_count(np, "row-gpios");
        pdata->num_col_gpios = of_gpio_named_count(np, "col-gpios");
        if (pdata->num_row_gpios <= 0 || pdata->num_col_gpios <= 0) {
                dev_err(dev, "%s: number of keypad rows/"
                             "columns not specified!\n", __func__);
                return ERR_PTR(-EINVAL);
        }

        pr_info("%s: num_col_gpios: %d;\n", __func__, pdata->num_col_gpios);
        pr_info("%s: num_row_gpios: %d;\n", __func__, pdata->num_row_gpios);

        pdata->wakeup = of_property_read_bool(np, "wakeup-source") ||
                        of_property_read_bool(np, "linux,wakeup");

        of_property_read_u32(np, "debounce-delay-ms",
                        &pdata->debounce_ms);
        of_property_read_u32(np, "col-scan-delay-us",
                        &pdata->col_scan_delay_us);

        pdata->row_gpios = (struct gpio_desc **)devm_kcalloc(dev,
                        pdata->num_row_gpios,
                        sizeof(struct gpio_desc *),
                        GFP_KERNEL);
        if (!pdata->row_gpios) {
                dev_err(dev, "%s: could not allocated memory!\n", __func__);
                return ERR_PTR(-ENOMEM);
        }

        pdata->col_gpios = (struct gpio_desc **)devm_kcalloc(dev,
                        pdata->num_col_gpios,
                        sizeof(struct gpio_desc *),
                        GFP_KERNEL);
        if (!pdata->col_gpios) {
                dev_err(dev, "%s: could not allocated memory!\n", __func__);
                return ERR_PTR(-ENOMEM);
        }

        for (i = 0; i < pdata->num_row_gpios; i++) {
                err = devm_gpiod_get_from_of_node(dev, np,
                                "row-gpios", i, GPIOD_IN, "row gpios");
                if (!err)
                        return ERR_PTR(-1);
                pdata->row_gpios[i] = err;
        }

        for (i = 0; i < pdata->num_col_gpios; i++) {
                err = devm_gpiod_get_from_of_node(dev, np,
                                "col-gpios", i, GPIOD_IN, "col gpios");
                if (!err)
                        return ERR_PTR(-1);
                pdata->col_gpios[i] = err;
        }

        return pdata;
}

static int matrix_keypad_probe(struct platform_device *pdev)
{
        matrix_keypad_st_t *keypad;
        struct device *dev = &pdev->dev;
        int err = 0;

        keypad = matrix_keypad_parse_dt(&pdev->dev);
        if (IS_ERR(keypad)) {
                dev_err(dev, "%s: probe failed: err_free_mem!\n", __func__);
                return 1;
        }
        dev_info(dev, "%s: Parsed device tree.\n", __func__);

        keypad->stopped = 0;
        keypad->scan_pending = 0;

        INIT_DELAYED_WORK(&keypad->dwork, matrix_keypad_scan);
        keypad->work_queue = alloc_workqueue("scan rows", WQ_UNBOUND, 1);
        if(!keypad->work_queue) {
                dev_err(dev, "%s: work_queue alloc failed!\n", __func__);
                destroy_workqueue(keypad->work_queue);
                return -ENOMEM;
        }

        spin_lock_init(&keypad->lock);
        init_waitqueue_head(&keypad->wait);

        err = matrix_keypad_init_gpio(pdev, keypad);
        if (err) {
                dev_err(dev, "%s: GPIO init error!\n", __func__);
                return -err;
        }

        dev_info(dev, "%s: inited gpio lines\n", __func__);

        device_init_wakeup(dev, keypad->wakeup);
        platform_set_drvdata(pdev, keypad);
        dev_info(dev, "%s: inited PM\n", __func__);

        keypad->mdev.minor      = MISC_DYNAMIC_MINOR;
        keypad->mdev.name       = DRIVER_NAME;
        keypad->mdev.fops       = &matrix_keypad_fops;
        keypad->mdev.parent     = &pdev->dev;

        err = misc_register(&keypad->mdev);
        if (err) {
                dev_err(dev, "%s: Cannot register misc device!\n", __func__);
                return err;
        }

        keypad->cols_state = (int *)devm_kzalloc(&pdev->dev,
                        sizeof(int) * keypad->num_col_gpios, GFP_KERNEL);
        if (!keypad->cols_state) {
                dev_err(dev, "%s: Could not allocate"
                                " memory for platform data!\n",  __func__);
                return -ENOMEM;
        }

        dev_err(dev, "%s: probe finished OK.\n", __func__);
        matrix_keypad_start(pdev);

        return 0;
}

static int matrix_keypad_remove(struct platform_device *dev)
{
        matrix_keypad_st_t *matrix_keypad = platform_get_drvdata(dev);

        matrix_keypad_stop(dev);

        if(matrix_keypad->work_queue)
                destroy_workqueue(matrix_keypad->work_queue);

        misc_deregister(&matrix_keypad->mdev);

        return 0;
}

static const struct of_device_id matrix_keypad_of_match[] = {
        { .compatible = "matrix-keypad" },
        {}
};
MODULE_DEVICE_TABLE(of, matrix_keypad_of_match);

static struct platform_driver matrix_keypad_driver = {
        .probe = matrix_keypad_probe,
        .remove = matrix_keypad_remove,
        .driver = {
                .name = DRIVER_NAME,
                .pm = &matrix_keypad_pm,
                .of_match_table = matrix_keypad_of_match,
        }
};

module_platform_driver(matrix_keypad_driver);

MODULE_AUTHOR("Mykyta Opaniuk <mykyta.opaniuk@globallogic.com>");
MODULE_DESCRIPTION("Matrix keyboard driver v1");
MODULE_LICENSE("GPL");
