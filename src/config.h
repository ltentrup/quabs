#ifndef __caqe_qcir__config__
#define __caqe_qcir__config__

#define SOLVER_MAJOR_VERSION 1
#define SOLVER_MINOR_VERSION 0

//#define PARALLEL_SOLVING

// disables runtime checks, e.g., check for undefined gates
//#define COMPETITION

#ifdef COMPETITION
#define NO_API_ASSERTIONS
#endif

#define LOGGING
//#define NDEBUG
#define CERTIFICATION

#endif /* defined(__caqe_qcir__config__) */
