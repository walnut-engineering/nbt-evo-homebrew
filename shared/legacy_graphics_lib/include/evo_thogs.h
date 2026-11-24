/*
 * QNX libthogs.so Integration Header
 * 
 * This header provides interface definitions for QNX's libthogs.so library,
 * a comprehensive CPU and thread monitoring system. Based on symbol analysis
 * of the native QNX library.
 * 
 * Features:
 * - Real-time CPU load monitoring (CCPULoadMonitor)
 * - Thread state observation and tracking
 * - Process and memory information APIs
 * - Event trigger system for automated monitoring
 * - Native QNX system integration
 * 
 * Usage: Link with -lthogs and ensure libthogs.so is available on target
 */

#ifndef EVO_THOGS_H
#define EVO_THOGS_H

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * CORE MONITORING DATA STRUCTURES
 * ============================================================================ */

/**
 * Thread information structure (based on symbol analysis)
 * Contains comprehensive thread state and performance data
 */
typedef struct {
    int tid;                    /* Thread ID */
    int pid;                    /* Process ID */
    char name[64];              /* Thread name */
    char state[16];             /* Thread state (READY, RUNNING, BLOCKED, etc.) */
    float cpu_load;             /* CPU load percentage */
    uint64_t cpu_time;          /* Total CPU time used */
    int priority;               /* Thread priority */
    uint32_t flags;             /* Thread flags */
} evo_thread_info_t;

/**
 * Process information structure
 */
typedef struct {
    pid_t pid;                  /* Process ID */
    char name[256];             /* Process name */
    char args[512];             /* Command line arguments */
    int num_threads;            /* Number of threads */
    float cpu_load;             /* Total process CPU load */
    uint64_t memory_usage;      /* Memory usage in bytes */
    int partition_id;           /* QNX partition ID */
} evo_process_info_t;

/**
 * System memory information
 */
typedef struct {
    uint64_t total_memory;      /* Total system memory */
    uint64_t free_memory;       /* Available memory */
    uint64_t used_memory;       /* Used memory */
    float memory_usage_percent; /* Memory usage percentage */
} evo_memory_info_t;

/* ============================================================================
 * TRIGGER SYSTEM DEFINITIONS
 * ============================================================================ */

/**
 * Trigger modes for event-based monitoring
 */
typedef enum {
    EVO_TRIGGER_ONCE = 0,       /* Fire trigger once */
    EVO_TRIGGER_CONTINUED = 1,  /* Continuous triggering */
    EVO_TRIGGER_BARRIER = 2     /* Barrier-style triggering */
} evo_trigger_mode_t;

/**
 * Trigger operations for condition checking
 */
typedef enum {
    EVO_TRIGGER_EQ = 0,         /* Equal */
    EVO_TRIGGER_NE = 1,         /* Not equal */
    EVO_TRIGGER_LT = 2,         /* Less than */
    EVO_TRIGGER_LE = 3,         /* Less than or equal */
    EVO_TRIGGER_GT = 4,         /* Greater than */
    EVO_TRIGGER_GE = 5          /* Greater than or equal */
} evo_trigger_op_t;

/**
 * Trigger items for different monitoring types
 */
typedef enum {
    EVO_TRIGGER_THREADLOAD = 0,     /* Thread load percentage */
    EVO_TRIGGER_IDLE = 1,           /* System idle percentage */
    EVO_TRIGGER_BLOCKINGTHREAD = 2, /* Thread blocking state */
    EVO_TRIGGER_TOOLEXIT = 3        /* Tool exit condition */
} evo_trigger_item_t;

/* ============================================================================
 * CORE API FUNCTIONS (Reverse-engineered from libthogs.so)
 * ============================================================================ */

/**
 * Memory Information Functions
 * These functions provide system memory statistics
 */
extern int _tbox_get_free_mem(uint64_t *free_memory);
extern int _tbox_get_total_mem(uint64_t *total_memory);

/**
 * Process Information Functions
 * Access detailed process and thread information
 */
extern int _tbox_get_pid_for_name(const char *process_name, pid_t *pid);
extern int _tbox_get_name_for_pid(pid_t pid, char *name, size_t name_size);
extern int _tbox_get_process_info(pid_t pid, evo_process_info_t *info);
extern int _tbox_get_process_arguments(pid_t pid, char *args, size_t args_size);
extern int _tbox_get_number_of_threads(pid_t pid, int *thread_count);

/**
 * Thread Management Functions
 * Thread enumeration and status queries
 */
extern int get_thread_status(int *status, uint32_t *flags, int tid, int pid);
extern int get_tid_by_tool_id(int tool_id, int *tid);
extern int get_tool_id_by_tid(int tid, int *tool_id);

/**
 * Partition Management Functions
 * QNX partition system access
 */
extern int _tbox_get_number_of_partitions(int *partition_count);
extern int _tbox_get_partitionid_by_name(const char *name, int *partition_id);
extern int _tbox_get_name_by_partitionid(int partition_id, char *name, size_t name_size);

/**
 * Monitoring Data Functions
 * Core monitoring infrastructure
 */
extern int get_monitor_data(int monitor_id, void *data, size_t data_size);
extern int set_monitor_data(int monitor_id, const void *data, size_t data_size);

/**
 * Observer Functions
 * Real-time state monitoring callbacks
 */
extern void idle_observer_func(float idle_percentage);
extern void threadload_observer_func(float load_percentage);
extern void blockingthread_observer_func(const char *thread_name);
extern void toolexit_observer_func(int exit_code);

/**
 * Trigger System Functions
 * Event-based monitoring triggers
 */
extern int trigger_default(int command_id, const void *data);
extern int set_tool_callback_func(void (*callback)(int, void*));

/* ============================================================================
 * HIGH-LEVEL WRAPPER FUNCTIONS
 * ============================================================================ */

/**
 * Initialize libthogs monitoring system
 * Returns 0 on success, -1 on error
 */
int evo_thogs_init(void);

/**
 * Cleanup libthogs resources
 */
void evo_thogs_cleanup(void);

/**
 * Get current system memory information
 * Returns 0 on success, -1 on error
 */
int evo_thogs_get_memory_info(evo_memory_info_t *mem_info);

/**
 * Get process information by name
 * Returns 0 on success, -1 on error
 */
int evo_thogs_get_process_by_name(const char *name, evo_process_info_t *proc_info);

/**
 * Get thread information for a process
 * Returns number of threads found, -1 on error
 */
int evo_thogs_get_thread_list(pid_t pid, evo_thread_info_t *threads, int max_threads);

/**
 * Get system idle percentage
 * Returns idle percentage (0.0-100.0), -1.0 on error
 */
float evo_thogs_get_idle_percentage(void);

/**
 * Set up CPU load monitoring trigger
 * Triggers callback when CPU load exceeds threshold
 * Returns 0 on success, -1 on error
 */
int evo_thogs_setup_cpu_trigger(float threshold, evo_trigger_op_t op, 
                                evo_trigger_mode_t mode, void (*callback)(float));

/**
 * Get top processes by CPU usage
 * Returns number of processes found, -1 on error
 */
int evo_thogs_get_top_processes(evo_process_info_t *processes, int max_processes);

/**
 * Get comprehensive system statistics
 * Fills provided structure with current system state
 * Returns 0 on success, -1 on error
 */
typedef struct {
    evo_memory_info_t memory;
    float system_idle;
    int total_processes;
    int total_threads;
    float average_cpu_load;
} evo_system_stats_t;

int evo_thogs_get_system_stats(evo_system_stats_t *stats);

/* ============================================================================
 * UTILITY MACROS
 * ============================================================================ */

/* Check if libthogs function succeeded */
#define EVO_THOGS_SUCCESS(result) ((result) == 0)
#define EVO_THOGS_FAILED(result)  ((result) != 0)

/* Convert bytes to human-readable format */
#define EVO_BYTES_TO_KB(bytes) ((bytes) / 1024)
#define EVO_BYTES_TO_MB(bytes) ((bytes) / (1024 * 1024))
#define EVO_BYTES_TO_GB(bytes) ((bytes) / (1024 * 1024 * 1024))

/* CPU load thresholds */
#define EVO_CPU_LOW_THRESHOLD    10.0f
#define EVO_CPU_NORMAL_THRESHOLD 50.0f
#define EVO_CPU_HIGH_THRESHOLD   80.0f
#define EVO_CPU_CRITICAL_THRESHOLD 95.0f

#ifdef __cplusplus
}
#endif

#endif /* EVO_THOGS_H */
