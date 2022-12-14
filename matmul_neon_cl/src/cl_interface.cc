#include "cl_interface.h"

CLInterface::CLInterface(int m, int n, int k): _m(m), _n(n), _k(k) {
    _a = _b = _c = nullptr;
}

CLInterface::~CLInterface() {
    if (_a) free(_a);
    if (_b) free(_b);
    if (_c) free(_c);
}

static void rand_init(float *a, int n) {
    for (int i = 0; i < n; i++)
        a[i] = static_cast<float>(rand() * 1.0 / RAND_MAX - 0.5);
}


static cl::Device get_cl_device(){
    
    std::vector<cl::Platform> platforms;
    cl::Platform::get(&platforms);

    if (platforms.empty()){
        std::cerr << "No platforms found!" << std::endl;
        exit(1);
    }

    auto platform = platforms.front();
    std::vector<cl::Device> devices;
    platform.getDevices(CL_DEVICE_TYPE_ALL, &devices);

    if (devices.empty()){
        std::cerr << "No devices found!" << std::endl;
        exit(1);
    }

    return devices.front();
}

void CLInterface::init() {
    // init _a, _b, _c
    _a = new float[_m * _k];
    _b = new float[_k * _n];
    _c = new float[_m * _n];
    rand_init(_a, _m * _k);
    rand_init(_b, _k * _n);

    // init cl
    _device = get_cl_device();
    #if defined(CL_FP32)
    std::ifstream kernel_file("./src/naive_cl_impl.cl");
    #elif defined(CL_ROW_FP32)
    std::ifstream kernel_file("./src/cl_row_impl.cl");
    #elif defined(CL_LMEM_FP32)
    std::ifstream kernel_file("./src/cl_lmem_impl.cl");
    #else 
        std::cout << "Error: must define corresponding CL macro.\n";
        exit(-1);
    #endif

    std::string src(std::istreambuf_iterator<char>(kernel_file), (std::istreambuf_iterator<char>()));

    cl::Program::Sources sources(1, std::make_pair(src.c_str(), src.length() + 1));
    _context = cl::Context(_device);
    _program = cl::Program(_context, sources);
    
    auto err = _program.build();
    if(err != CL_BUILD_SUCCESS){
        std::cerr << "Error!\nBuild Status: " << _program.getBuildInfo<CL_PROGRAM_BUILD_STATUS>(_device) 
        << "\nBuild Log:\t " << _program.getBuildInfo<CL_PROGRAM_BUILD_LOG>(_device) << std::endl;
        exit(1);
    }
}

void CLInterface::naive_matmul(float *a, float *b, float *c) {
    for (int p = 0; p < _k; p++)
        for (int i = 0; i < _m; i++)
            for (int j = 0; j < _n; j++)
                c[i * _n + j] += a[i + p * _m] * b[p * _n + j];
}

void CLInterface::run_once() {
    cl::Buffer aBuf(_context, CL_MEM_READ_ONLY | CL_MEM_HOST_NO_ACCESS | CL_MEM_COPY_HOST_PTR, _m * _k * sizeof(float), _a);
    cl::Buffer bBuf(_context, CL_MEM_READ_ONLY | CL_MEM_HOST_NO_ACCESS | CL_MEM_COPY_HOST_PTR, _k * _n * sizeof(float), _b);
    cl::Buffer cBuf(_context, CL_MEM_READ_WRITE | CL_MEM_HOST_READ_ONLY, _m * _n * sizeof(float));

    cl::Kernel kernel(_program, "matmul");
    kernel.setArg(0, aBuf);
    kernel.setArg(1, bBuf);
    kernel.setArg(2, cBuf);
    kernel.setArg(3, sizeof(int), &_m);
    kernel.setArg(4, sizeof(int), &_n);
    kernel.setArg(5, sizeof(int), &_k);

    #ifdef CL_LMEM_FP32
    kernel.setArg(6, cl::Local(_k * sizeof(float)));
    #endif

    cl::CommandQueue queue(_context, _device, CL_QUEUE_PROFILING_ENABLE);

    #if defined(CL_FP32)
        queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(_m, _n), cl::NDRange(_m / 128, _n / 128));

    #elif defined(CL_ROW_FP32)
        queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(_m));
    #elif defined(CL_LMEM_FP32)
        queue.enqueueNDRangeKernel(kernel, cl::NullRange, cl::NDRange(_m), cl::NDRange(_m / 32));
    #else 
        std::cout << "Error: must declare corresponding CL macro\n";
        exit(-1);
    #endif

    queue.enqueueReadBuffer(cBuf, CL_TRUE, 0, _m * _n * sizeof(float), _c);
    queue.finish();
}

void CLInterface::validate_impl() {
    float *d = new float[_m * _n];
    memset(d, 0, _m * _n * sizeof(float));
    memset(_c, 0, _m * _n * sizeof(float));
    naive_matmul(_a, _b, d);
    run_once();

    for (int i = 0; i < _m; i++)
        for (int j = 0; j < _n; j++)
            if (ABS(_c[i * _n + j] - d[i * _n + j]) > 1e-4) {
                std::cout << "Validation failed. Exit.\n";
                printf("(i,j)=(%d,%d), (c,d)=(%.2f, %.2f).\n", i, j, _c[i * _n + j], d[i * _n + j]);
                exit(-1);
            }

    delete []d;
}