#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOCommandGate.h>
#include <string.h>

#define STATUS_IDLE 0x0
#define STATUS_WRITE_IN_PROGRESS 0x1
#define STATUS_READ_IN_PROGRESS 0x2

#define TIMEOUT 20

#define DW_IC_CON_MASTER 0x1
#define DW_IC_CON_SPEED_FAST 0x4
#define DW_IC_CON_RESTART_EN 0x20

#define I2C_FUNC_I2C 0x00000001


#define DW_IC_CON 0x0
#define DW_IC_TAR 0x4
#define DW_IC_DATA_CMD 0x10
#define DW_IC_SS_SCL_HCNT 0x14
#define DW_IC_SS_SCL_LCNT 0x18
#define DW_IC_FS_SCL_HCNT 0x1c
#define DW_IC_FS_SCL_LCNT 0x20
#define DW_IC_INTR_STAT 0x2c
#define DW_IC_INTR_MASK 0x30
#define DW_IC_RAW_INTR_STAT 0x34
#define DW_IC_RX_TL 0x38
#define DW_IC_TX_TL 0x3c
#define DW_IC_CLR_INTR 0x40
#define DW_IC_CLR_RX_UNDER 0x44
#define DW_IC_CLR_RX_OVER 0x48
#define DW_IC_CLR_TX_OVER 0x4c
#define DW_IC_CLR_RD_REQ 0x50
#define DW_IC_CLR_TX_ABRT 0x54
#define DW_IC_CLR_RX_DONE 0x58
#define DW_IC_CLR_ACTIVITY 0x5c
#define DW_IC_CLR_STOP_DET 0x60
#define DW_IC_CLR_START_DET 0x64
#define DW_IC_CLR_GEN_CALL 0x68
#define DW_IC_ENABLE 0x6c
#define DW_IC_STATUS 0x70
#define DW_IC_TXFLR 0x74
#define DW_IC_RXFLR 0x78
#define DW_IC_SDA_HOLD 0x7c
#define DW_IC_TX_ABRT_SOURCE 0x80
#define DW_IC_ENABLE_STATUS 0x9c
#define DW_IC_COMP_VERSION 0xf8
#define DW_IC_SDA_HOLD_MIN_VERS 0x3131312A
#define DW_IC_COMP_TYPE 0xfc
#define DW_IC_COMP_TYPE_VALUE 0x44570140

#define DW_IC_STATUS_ACTIVITY 0x1

#define DW_IC_ERR_TX_ABRT 0x1

#define DW_IC_CON_10BITADDR_MASTER 0x10

#define DW_IC_INTR_RX_UNDER 0x001
#define DW_IC_INTR_RX_OVER 0x002
#define DW_IC_INTR_RX_FULL 0x004
#define DW_IC_INTR_TX_OVER 0x008
#define DW_IC_INTR_TX_EMPTY 0x010
#define DW_IC_INTR_RD_REQ 0x020
#define DW_IC_INTR_TX_ABRT 0x040
#define DW_IC_INTR_RX_DONE 0x080
#define DW_IC_INTR_ACTIVITY 0x100
#define DW_IC_INTR_STOP_DET 0x200
#define DW_IC_INTR_START_DET 0x400
#define DW_IC_INTR_GEN_CALL 0x800

#define DW_IC_INTR_DEFAULT_MASK     (DW_IC_INTR_RX_FULL | \
                                     DW_IC_INTR_TX_EMPTY | \
                                     DW_IC_INTR_TX_ABRT | \
                                     DW_IC_INTR_STOP_DET)


#define BIT(nr) (1UL << (nr))
#define DW_IC_TAR_10BITADDR_MASTER BIT(12);

#define I2C_SMBUS_BLOCK_MAX 32

#define I2C_SMBUS_QUICK 0
#define I2C_SMBUS_BYTE 1
#define I2C_SMBUS_BYTE_DATA 2
#define I2C_SMBUS_WORD_DATA 3
#define I2C_SMBUS_PROC_CALL 4
#define I2C_SMBUS_BLOCK_DATA 5
#define I2C_SMBUS_I2C_BLOCK_BROKEN 6
#define I2C_SMBUS_BLOCK_PROC_CALL 7
#define I2C_SMBUS_I2C_BLOCK_DATA 8


#define I2C_SMBUS_READ 1
#define I2C_SMBUS_WRITE 0

#define EAGAIN 35

#define RMI_PAGE_SELECT_REGISTER 0xff
#define RMI_I2C_PAGE(addr) (((addr) >> 8) & 0x0ff)

#define PDT_START_SCAN_LOCATION 0x00e9
#define PDT_END_SCAN_LOCATION 0x0005

#define RMI4_END_OF_PDT(id) ((id) == 0x00 || (id) == 0xff)
#define RMI4_MAX_PAGE 0xff
#define RMI4_PAGE_SIZE 0x100

#define RMI_PRODUCT_ID_LENGTH 10

class VoodooI2C : public IOService {
    
    OSDeclareDefaultStructors(VoodooI2C);
    
    
public:
    IOACPIPlatformDevice* fACPIDevice;
    
    struct i2c_msg {
        UInt16 addr;
        UInt16 flags;
        UInt16 len;
        UInt8 *buf;
        
#define I2C_M_TEN 0x0010
#define I2C_M_RD 0x0001
#define I2C_M_RECV_LEN 0x0400
    };
    

    
    typedef struct {
        UInt8 byte;
        UInt16 word;
        UInt8 block[I2C_SMBUS_BLOCK_MAX + 2];
    } i2c_smbus_data;
    
    typedef struct {
        IOACPIPlatformDevice *provider;
        
        IOWorkLoop *workLoop;
        IOInterruptEventSource *interruptSource;
        
        IOCommandGate *commandGate;
        
        IOMemoryMap *mmap;
        IOVirtualAddress mmio;
        
        UInt32 clk_rate_khz;
        UInt32 sda_hold_time;
        UInt32 sda_falling_time;
        UInt32 scl_falling_time;
        
        UInt32 functionality;
        UInt32 master_cfg;
        
        UInt tx_fifo_depth;
        UInt rx_fifo_depth;
        
        UInt32 ss_hcnt;
        UInt32 ss_lcnt;
        
        UInt32 fs_hcnt;
        UInt32 fs_lcnt;
    
        struct i2c_algorithm *algo;
        
        bool commandComplete = false;
        struct i2c_msg *msgs;
        int msgs_num;
        int cmd_err;
        int msg_write_idx;
        int msg_read_idx;
        int msg_err;
        UInt status;
        UInt32 abort_source;
        int rx_outstanding;
        
        UInt32 rx_buf_len;
        UInt8 *rx_buf;
        UInt32 tx_buf_len;
        UInt8 *tx_buf;
        
        int retries = 5;
        char *name;
        
        bool ready;
        
    } I2CBus;
    
    I2CBus* _dev;
    
    typedef struct {
        I2CBus *phys;
        UInt16 addr;
        char read_write;
        UInt8 command;
        int size;
        i2c_smbus_data *data;
    } commandGateTransaction;
    
    typedef struct {
        int page;
    } rmi_i2c_data;
    
    struct rmi_function_descriptor {
        UInt16 query_base_addr;
        UInt16 command_base_addr;
        UInt16 control_base_addr;
        UInt16 data_base_addr;
        UInt8 interrupt_source_count;
        UInt8 function_number;
    };
    
    struct rmi_function_container {
        //struct list_head list;
        
        struct rmi_function_descriptor fd;
        //RMI4Device *rmi_dev;
        struct rmi_function_handler *fh;
        int num_of_irqs;
        int irq_pos;
        
        void *data;
        
    };
    
    struct rmi_driver_data {
        struct rmi_function_container rmi_functions;
        
        struct rmi_function_descriptor f01_fd;
        int f01_num_of_irqs;
        int f01_irq_pos;
        
        UInt8 manufacturer_id;
        
        UInt8 product_id[RMI_PRODUCT_ID_LENGTH + 1];
        
    };
    
    typedef struct {
        
        unsigned short addr;
        
        rmi_i2c_data *data;
        
        char* name;
        
        I2CBus* _dev;
        
        IOACPIPlatformDevice* provider;
        
        IOWorkLoop* workLoop;
        IOCommandGate* commandGate;
        
        IOInterruptEventSource *interruptSource;
        
        rmi_driver_data *driver_data;
        
    } RMI4Device;
    
    RMI4Device* _rmidev;
    
    
    static void getACPIParams(IOACPIPlatformDevice* fACPIDevice, char method[], UInt32 *hcnt, UInt32 *lcnt, UInt32 *sda_hold);
    bool acpiConfigure(I2CBus* _dev);
    void disableI2CInt(I2CBus* _dev);
    void enableI2CDevice(I2CBus*, bool enabled);
    UInt32 funcI2C(I2CBus* _dev);
    virtual char* getMatchedName(IOService* provider);
    int handleTxAbortI2C(I2CBus* _dev);
    bool initI2CBus(I2CBus* _dev);
    virtual bool mapI2CMemory(I2CBus* _dev);
    void readI2C(I2CBus* _dev);
    UInt32 readl(I2CBus* _dev, int offset);
    UInt32 readClearIntrbitsI2C(I2CBus* _dev);
    void releaseAllI2CChildren();
    void setI2CPowerState(I2CBus* _dev, bool enabled);
    virtual bool start(IOService* provider);
    virtual void stop(IOService* provider);
    int waitBusNotBusyI2C(I2CBus* _dev);
    void writel(I2CBus* _dev, UInt32 b, int offset);
    int xferI2C(I2CBus* _dev, i2c_msg *msgs, int num);
    void xferInitI2C(I2CBus* _dev);
    void xferMsgI2C(I2CBus* _dev);
    
    void interruptOccured(OSObject* owner, IOInterruptEventSource* src, int intCount);
    void RMI4InterruptOccured(OSObject* owner, IOInterruptEventSource* src, int intCount);
    
    //static I2CBus* getBusByName(char* name );
    
    SInt32 i2c_smbus_xfer(I2CBus *phys,
                                   UInt16 addr, char read_write, UInt8 command, int size,
                                   i2c_smbus_data *data);
    SInt32 i2c_smbus_xfer_gated(commandGateTransaction *transaction);
    
    SInt32 i2c_smbus_write_byte_data(I2CBus *phys, UInt16 addr, UInt8 command, UInt8 value);
    
    SInt32 i2c_smbus_read_i2c_block_data(I2CBus* phys, UInt16 addr, UInt8 command, UInt8 length, UInt8 *values);
    SInt32 i2c_smbus_write_i2c_block_data(I2CBus* phys, UInt16 addr, UInt8 command, UInt8 length, const UInt8 *values);
    
    int i2c_transfer(I2CBus* phys, i2c_msg *msgs, int num);
    int __i2c_transfer(I2CBus* phys, i2c_msg *msgs, int num);
    //static int i2c_smbus_write_i2c_block_data;
    
    //static char* getName();
    
    int rmi_set_page(RMI4Device *phys, UInt page);
    
    
    void clearI2CInt(I2CBus* _dev);
    
    int initRMI4Device(RMI4Device* _rmidev);
    
    int probeRMI4Device(RMI4Device *phys);
    
    int rmi_driver_f01_init(RMI4Device *rmi_dev);
    
    int rmi_i2c_write_block(RMI4Device *phys, UInt16 addr, UInt8 *buf, int len);
    
    int rmi_i2c_write_block_gated(RMI4Device *phys, UInt16 *addr, UInt8 *buf, int *len);
    
    int rmi_i2c_write(RMI4Device *phys, UInt16 addr, UInt8 data);
    
    int rmi_i2c_read_block(RMI4Device *phys, UInt16 addr, UInt8 *buf, int len);
    
    int rmi_i2c_read_block_gated(RMI4Device *phys, UInt16 *addr, UInt8 *buf, int *len);
    
    int rmi_i2c_read(RMI4Device *phys, UInt16 addr, UInt8 *buf);
};