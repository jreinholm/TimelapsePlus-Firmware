#include <avr/io.h>
#include "settings.h"
#include "debug.h"
#include "tldefs.h"
#include "notify.h"

extern settings_t conf;

/******************************************************************
 *
 *   Notify Class
 *   Watches variables and calls handlers on changes
 *
 ******************************************************************/
Notify::Notify()
{
    return;
}

void Notify::task()
{
    uint8_t i;

    for(i = 0; i < MAX_ITEMS_WATCHED; i++)
    {
        if(watchedItems[i].active)
        {
            uint8_t x, sum = 0, xsum = 0;
            for(x = 0; x < watchedItems[i].size; x++)
            {
                xsum ^= ((char *) watchedItems[i].item)[x];
                sum += ((char *) watchedItems[i].item)[x];
            }
            if(sum != watchedItems[i].chksum || xsum != watchedItems[i].chkxsum)
            {
                watchedItems[i].chksum = sum;
                watchedItems[i].chkxsum = xsum;
                // Run Notification Handler
                ((void (*)(uint8_t))watchedItems[i].handler)(watchedItems[i].id);
            }
        }
    }
}

void Notify::watch(uint8_t id, void * item, uint8_t size, void (handler)(uint8_t))
{
    uint8_t i;
    unWatch(id, handler);
    for(i = 0; i < MAX_ITEMS_WATCHED; i++)
    {
        if(!watchedItems[i].active)
        {
            watchedItems[i].active = 1;
            watchedItems[i].id = id;
            watchedItems[i].size = size;
            watchedItems[i].item = item;
            watchedItems[i].handler = (void*)handler;
            uint8_t x, sum = 0, xsum = 0;
            for(x = 0; x < size; x++)
            {
                xsum ^= ((char *) item)[x];
                sum += ((char *) item)[x];
            }
            watchedItems[i].chksum = sum;
            watchedItems[i].chkxsum = xsum;
            return;
        }
    }
    DEBUG(STR("ERROR: Watch Buffer Full!\n\r"));
    return;
}

void Notify::unWatch(uint8_t id)
{
    uint8_t i;
    for(i = 0; i < MAX_ITEMS_WATCHED; i++)
    {
        if(watchedItems[i].active && watchedItems[i].id == id)
        {
            watchedItems[i].active = 0;
        }
    }
    return;
}

void Notify::unWatch(uint8_t id, void (handler)(uint8_t))
{
    uint8_t i;
    for(i = 0; i < MAX_ITEMS_WATCHED; i++)
    {
        if(watchedItems[i].active && watchedItems[i].id == id && watchedItems[i].handler == (void*)handler)
        {
            watchedItems[i].active = 0;
        }
    }
    return;
}

void Notify::unWatch(void (handler)(uint8_t))
{
    uint8_t i;
    for(i = 0; i < MAX_ITEMS_WATCHED; i++)
    {
        if(watchedItems[i].active && watchedItems[i].handler == (void*)handler)
        {
            watchedItems[i].active = 0;
        }
    }
    return;
}


