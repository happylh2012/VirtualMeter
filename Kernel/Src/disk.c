/**
 * @brief		
 * @details		
 * @date		2018-08-30
 **/

/* Includes ------------------------------------------------------------------*/
#include "kernel.h"
#include "allocator.h"
#include "allocator_ctrl.h"
#include "tasks.h"
#include "string.h"

#include "cpu.h"
#include "eeprom.h"
#include "flash.h"

/* Private typedef -----------------------------------------------------------*/
/**
  * @brief  文件读写特性
  */
enum __attr
{
    FILE_SAFE = 0x03,//带冗余备份的安全区域(可单字节擦写存储介质)
    FILE_FREQ = 0x06,//可频繁修改区域(单字节擦写存储介质)
    FILE_RARE = 0x0c,//不频繁修改区域(块擦除介质 占用双倍存储空间并且写入时阻塞时间较长)
    FILE_LOOP = 0x18,//文件循环写入(块擦除介质)
    FILE_ONCE = 0x30,//文件一次性写入(块擦除介质)
};

/**
  * @brief  
  */
struct __file_entry
{
    char						*name;
    uint32_t					size;
    enum __attr                 attr;
};

/* Private define ------------------------------------------------------------*/
#define EEP_PAGES_DIV_RATIO     ((uint8_t)2)//取值0~8，代表冗余备份区占用的页的比例
#define EEP_PAGES_SAFE(n)       ((uint32_t)((n)*EEP_PAGES_DIV_RATIO/8))//冗余备份区占用页数
#define EEP_PAGES_FREQ(n)       ((uint32_t)((n)*(8-EEP_PAGES_DIV_RATIO)/8))//频繁修改区占用页数

/* Private macro -------------------------------------------------------------*/
#define AMOUNT_FILE			    ((uint16_t)(sizeof(file_entry)/sizeof(struct __file_entry)))

/* Private variables ---------------------------------------------------------*/
/**
  * @brief  系统文件表
  */
static const struct __file_entry file_entry[] = 
{
    /* 文件名  文件大小  文件所在分区 */
	{"display", 4*1024, FILE_FREQ}, //显示参数
    {"hdlc", 64, FILE_FREQ}, //HDLC协议参数
    {"dlms", 4*1024, FILE_FREQ}, //DLMS协议参数
    {"lexicon", 64*1024, FILE_ONCE}, //电表数据项词典
    {"disconnect", 512, FILE_FREQ}, //继电器参数
    {"firmware", 512*1024, FILE_ONCE}, //固件升级
};


static uint8_t lock = 0;

/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/
static void disk_ctrl_start(void)
{
    if(system_status() == SYSTEM_RUN)
    {
        cpu.watchdog.feed();
        eeprom.control.init(DEVICE_NORMAL);
        
        cpu.watchdog.feed();
        flash.control.init(DEVICE_NORMAL);
    }
    else
    {
        cpu.watchdog.feed();
        eeprom.control.init(DEVICE_LOWPOWER);
        
        cpu.watchdog.feed();
        flash.control.init(DEVICE_LOWPOWER);
    }
}

/**
  * @brief  
  */
static void disk_ctrl_unlock(void)
{
    lock = 0x5a;
}

/**
  * @brief  
  */
static void disk_ctrl_lock(void)
{
    lock = 0;
}

/**
  * @brief  
  */
static void disk_ctrl_idle(void)
{
    cpu.watchdog.feed();
    eeprom.control.suspend();
    
    cpu.watchdog.feed();
	flash.control.suspend();
}

/**
  * @brief  
  */
static void disk_ctrl_format(void)
{
    cpu.watchdog.feed();
    eeprom.erase();
    
    cpu.watchdog.feed();
	flash.erase();
}

/**
  * @brief  
  */
const struct __disk_ctrl disk_ctrl = 
{
    .start              = disk_ctrl_start,
    .lock               = disk_ctrl_lock,
    .unlock             = disk_ctrl_unlock,
    .idle               = disk_ctrl_idle,
    .format             = disk_ctrl_format,
};


/**
  * @brief  
  */
static uint32_t disk_read(const char *name, uint32_t offset, uint32_t count, void *buff)
{
	uint16_t loop;
	uint16_t index = 0xffff;
	uint32_t address = 0;
    
    uint32_t pagesize = 0;
    uint32_t page = 0;
    uint32_t header = 0;
    uint32_t middle = 0;
    uint32_t tail = 0;
    uint8_t *buffer = (uint8_t *)buff;
	
	if(!name || !count || !buff)
	{
		return(0);
	}
	
	for(loop=0; loop<AMOUNT_FILE; loop++)
	{
		if(strcmp(file_entry[loop].name, name) == 0)
		{
			index = loop;
			break;
		}
	}
	
	if(index == 0xffff)
	{
		return(0);
	}
    
    if(file_entry[index].attr == FILE_SAFE)
    {
        //...
        return(0);
    }
    else if(file_entry[index].attr == FILE_FREQ)
    {
        pagesize = eeprom.info.pagesize();
        
        if(!pagesize)
        {
            return(0);
        }
        
        for(loop=0; loop<index; loop++)
        {
            if(file_entry[loop].attr == FILE_FREQ)
            {
                address += file_entry[loop].size;
            }
        }
        
        address += offset;//计算起始地址
        page = address / pagesize;//计算起始页
        header = address % pagesize;//计算起始页内起始地址
        
        if(page == ((address + count - 1) / pagesize))
        {
            //不分页
            if(eeprom.page.read(page, header, count, buffer) != count)
            {
                return(0);
            }
            
            return(count);
        }
        else
        {
            //分页
            middle = (count - ((pagesize - header) % pagesize)) / pagesize;//计算整页页数
            tail = (address + count) % pagesize;//计算最后一页写入字节数
            
            if(header)
            {
                if(eeprom.page.read(page, header, (pagesize - header), buffer) != (pagesize - header))
                {
                    return(0);
                }
                
                buffer += (pagesize - header);
                page += 1;
            }
            
            if(middle)
            {
                for(loop=0; loop<middle; loop++)
                {
                    if(eeprom.page.read(page, 0, pagesize, buffer) != pagesize)
                    {
                        return(0);
                    }
                    
                    buffer += pagesize;
                    page += 1;
                }
            }
            
            if(tail)
            {
                if(eeprom.page.read(page, 0, tail, buffer) != tail)
                {
                    return(0);
                }
            }
            
            return(count);
        }
    }
    else if(file_entry[index].attr == FILE_RARE)
    {
        //...
        return(0);
    }
    else if(file_entry[index].attr == FILE_LOOP)
    {
        //...
        return(0);
    }
    else if(file_entry[index].attr == FILE_ONCE)
    {
        //...
        return(0);
    }
    else
    {
        return(0);
    }
}

/**
  * @brief  
  */
static uint32_t disk_write(const char *name, uint32_t offset, uint32_t count, const void *buff)
{
	uint16_t loop;
	uint16_t index = 0xffff;
	uint32_t address = 0;
    
    uint32_t pagesize = 0;
    uint32_t page = 0;
    uint32_t header = 0;
    uint32_t middle = 0;
    uint32_t tail = 0;
    uint8_t *buffer = (uint8_t *)buff;
	
	if(!name || !count || !buff)
	{
		return(0);
	}
    
    if(lock != 0x5a)
    {
        return(0);
    }
	
	for(loop=0; loop<AMOUNT_FILE; loop++)
	{
		if(strcmp(file_entry[loop].name, name) == 0)
		{
			index = loop;
			break;
		}
	}
	
	if(index == 0xffff)
	{
		return(0);
	}
	
    if(file_entry[index].attr == FILE_SAFE)
    {
        //...
        return(0);
    }
    else if(file_entry[index].attr == FILE_FREQ)
    {
        pagesize = eeprom.info.pagesize();
        
        if(!pagesize)
        {
            return(0);
        }
        
        for(loop=0; loop<index; loop++)
        {
            if(file_entry[loop].attr == FILE_FREQ)
            {
                address += file_entry[loop].size;
            }
        }
        
        address += offset;//计算起始地址
        page = address / pagesize;//计算起始页
        header = address % pagesize;//计算起始页内起始地址
        
        if(page == ((address + count - 1) / pagesize))
        {
            //不分页
            if(eeprom.page.write(page, header, count, buffer) != count)
            {
                return(0);
            }
            
            return(count);
        }
        else
        {
            //分页
            middle = (count - ((pagesize - header) % pagesize)) / pagesize;//计算整页页数
            tail = (address + count) % pagesize;//计算最后一页写入字节数
            
            if(header)
            {
                if(eeprom.page.write(page, header, (pagesize - header), buffer) != (pagesize - header))
                {
                    return(0);
                }
                
                buffer += (pagesize - header);
                page += 1;
            }
            
            if(middle)
            {
                for(loop=0; loop<middle; loop++)
                {
                    if(eeprom.page.write(page, 0, pagesize, buffer) != pagesize)
                    {
                        return(0);
                    }
                    
                    buffer += pagesize;
                    page += 1;
                }
            }
            
            if(tail)
            {
                if(eeprom.page.write(page, 0, tail, buffer) != tail)
                {
                    return(0);
                }
            }
            
            return(count);
        }
    }
    else if(file_entry[index].attr == FILE_RARE)
    {
        //...
        return(0);
    }
    else if(file_entry[index].attr == FILE_LOOP)
    {
        //...
        return(0);
    }
    else if(file_entry[index].attr == FILE_ONCE)
    {
        //...
        return(0);
    }
    else
    {
        return(0);
    }
}

/**
  * @brief  
  */
static uint32_t disk_size(const char *name)
{
	uint16_t loop;
	
	if(!name)
	{
		return(0);
	}
	
	for(loop=0; loop<AMOUNT_FILE; loop++)
	{
		if(strcmp(file_entry[loop].name, name) == 0)
		{
			return(file_entry[loop].size);
		}
	}
    
    return(0);
}

/**
  * @brief  
  */
struct __file file = 
{
	.read				= disk_read,
	.write				= disk_write,
    .size               = disk_size,
};
