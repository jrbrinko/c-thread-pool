#ifndef rc_h
#define rc_h

typedef enum rc_st {
    Success,
    OutOfMemory,
    InvalidArgument,
    InvalidOperation,
    Timeout, 
    Error,
    QueueFull,
    QueueEmpty,
} rc_t;

#endif