#include "condy/async_operations.hpp"
#include "condy/coro.hpp"
#include "condy/detail/ring.hpp"
#include "condy/helpers.hpp"
#include "condy/runtime.hpp"
#include "condy/runtime_options.hpp"
#include "condy/sync_wait.hpp"
#include "condy/zcrx.hpp"
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <doctest.h>
#include <sys/mman.h>

#if CONDY_URING_VERSION_GE(2, 15) // >= 2.15

namespace {

inline condy::Runtime make_zcrx_runtime() {
    return condy::Runtime(
        condy::RuntimeOptions().enable_cqe32().enable_defer_taskrun());
}

condy::detail::Ring &enable_runtime_ring(condy::Runtime &runtime) {
    auto &ring = runtime.ring_internal();
    int r = io_uring_enable_rings(ring.ring());
    REQUIRE(r == 0);
    return ring;
}

static io_uring_cqe mock_cqe[2];

} // namespace

TEST_CASE("test zcrx - pool init in coro") {
    auto func = []() -> condy::Coro<void> {
        condy::Runtime runtime = make_zcrx_runtime();
        condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                         condy::ZeroCopyRxArea{.size = 4096});
        co_return;
    };
    condy::sync_wait(func());
}

TEST_CASE("test zcrx - pool init with explicit runtime") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});
}

TEST_CASE("test zcrx - pool init with external area") {
    condy::Runtime runtime = make_zcrx_runtime();

    constexpr size_t area_size = 4ul * 4096;
    void *area = mmap(nullptr, area_size, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    // REQUIRE(area != MAP_FAILED);
    auto d = condy::detail::defer([&]() { munmap(area, area_size); });

    condy::ZeroCopyRxBufferPool pool(
        runtime, 4, condy::ZeroCopyRxArea{.addr = area, .size = area_size});
}

TEST_CASE("test zcrx - pool before ring enabled") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});
    enable_runtime_ring(runtime);
}

TEST_CASE("test zcrx - handle_finish success") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 1024;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 0;

    condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
    REQUIRE(buf.owns_buffer());
    REQUIRE(buf.data() != nullptr);
    REQUIRE(buf.size() == 1024);
}

TEST_CASE("test zcrx - handle_finish error") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = -ENOMEM;

    condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
    REQUIRE_FALSE(buf.owns_buffer());
    REQUIRE(buf.data() == nullptr);
    REQUIRE(buf.size() == 0);
}

TEST_CASE("test zcrx - handle_finish offset") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 512;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 0;
    condy::ZeroCopyRxBuffer buf1 = pool.handle_finish(mock_cqe);

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 256;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 512;
    condy::ZeroCopyRxBuffer buf2 = pool.handle_finish(mock_cqe);

    REQUIRE(static_cast<char *>(buf2.data()) ==
            static_cast<char *>(buf1.data()) + 512);
    REQUIRE(buf2.size() == 256);
}

TEST_CASE("test zcrx - handle_finish area token masked out") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 128;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off =
        64 | (1ULL << 48);

    condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
    REQUIRE(buf.owns_buffer());
    REQUIRE(buf.size() == 128);

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 128;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 64;
    condy::ZeroCopyRxBuffer buf2 = pool.handle_finish(mock_cqe);
    REQUIRE(buf.data() == buf2.data());
}

TEST_CASE("test zcrx - buffer reset returns to pool") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 256;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 0;

    condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
    void *data_ptr = buf.data();
    REQUIRE(buf.owns_buffer());

    buf.reset();
    REQUIRE(!buf.owns_buffer());
    REQUIRE(buf.data() == nullptr);
    REQUIRE(buf.size() == 0);

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 256;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 0;
    condy::ZeroCopyRxBuffer buf2 = pool.handle_finish(mock_cqe);
    REQUIRE(buf2.data() == data_ptr);
}

TEST_CASE("test zcrx - buffer is also buffer") {
    condy::ZeroCopyRxBuffer buf;
    auto fixed_buf = condy::fixed(2, buf);
    [[maybe_unused]] auto aw = condy::async_read(0, fixed_buf, 0);
}

TEST_CASE("test zcrx - buffer data access") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 64;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 128;

    condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
    REQUIRE(buf.data() != nullptr);
    REQUIRE(buf.size() == 64);

    std::memset(buf.data(), 0xAB, buf.size());
    REQUIRE(static_cast<unsigned char *>(buf.data())[0] == 0xAB);
    REQUIRE(static_cast<unsigned char *>(buf.data())[63] == 0xAB);
}

TEST_CASE("test zcrx - multiple consecutive handle_finish") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});

    for (int i = 0; i < 4; i++) {
        std::memset(mock_cqe, 0, sizeof(mock_cqe));
        mock_cqe->res = 512;
        reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off =
            static_cast<uint64_t>(i) * 512;

        condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
        REQUIRE(buf.owns_buffer());
        REQUIRE(buf.size() == 512);
    }
}

TEST_CASE("test zcrx - interleaved handle_finish and add_buffer_back") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 1024;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 0;
    condy::ZeroCopyRxBuffer buf1 = pool.handle_finish(mock_cqe);
    void *data1 = buf1.data();
    buf1.reset();

    mock_cqe->res = 1024;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 2048;
    condy::ZeroCopyRxBuffer buf2 = pool.handle_finish(mock_cqe);
    REQUIRE(buf2.data() != data1);
    buf2.reset();

    mock_cqe->res = 1024;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 0;
    condy::ZeroCopyRxBuffer buf3 = pool.handle_finish(mock_cqe);
    REQUIRE(buf3.data() == data1);
}

TEST_CASE("test zcrx - external area handle_finish") {
    condy::Runtime runtime = make_zcrx_runtime();

    constexpr size_t area_size = 2ul * 4096;
    void *area = mmap(nullptr, area_size, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    // REQUIRE(area != MAP_FAILED);
    auto d = condy::detail::defer([&]() { munmap(area, area_size); });

    condy::ZeroCopyRxBufferPool pool(
        runtime, 4, condy::ZeroCopyRxArea{.addr = area, .size = area_size});

    std::memset(static_cast<char *>(area) + 1024, 0xCD, 256);

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 256;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 1024;

    condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
    REQUIRE(buf.owns_buffer());
    REQUIRE(buf.size() == 256);
    REQUIRE(static_cast<char *>(buf.data()) >= static_cast<char *>(area));
    REQUIRE(static_cast<char *>(buf.data()) <
            static_cast<char *>(area) + area_size);
    REQUIRE(static_cast<unsigned char *>(buf.data())[0] == 0xCD);
    REQUIRE(static_cast<unsigned char *>(buf.data())[255] == 0xCD);
}

TEST_CASE("test zcrx - external area survives pool destruction") {
    condy::Runtime runtime = make_zcrx_runtime();

    constexpr size_t area_size = 2ul * 4096;
    void *area = mmap(nullptr, area_size, PROT_READ | PROT_WRITE,
                      MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
    // REQUIRE(area != MAP_FAILED);
    std::memset(area, 0xAB, area_size);

    {
        condy::ZeroCopyRxBufferPool pool(
            runtime, 4, condy::ZeroCopyRxArea{.addr = area, .size = area_size});
    }

    REQUIRE(static_cast<unsigned char *>(area)[0] == 0xAB);
    REQUIRE(static_cast<unsigned char *>(area)[area_size - 1] == 0xAB);

    munmap(area, area_size);
}

TEST_CASE("test zcrx - default constructed buffer") {
    condy::ZeroCopyRxBuffer buf;
    REQUIRE_FALSE(buf.owns_buffer());
    REQUIRE(buf.data() == nullptr);
    REQUIRE(buf.size() == 0);

    buf.reset();
    REQUIRE_FALSE(buf.owns_buffer());

    condy::ZeroCopyRxBuffer buf2;
    buf2 = std::move(buf);
    REQUIRE_FALSE(buf2.owns_buffer());
}

TEST_CASE("test zcrx - buffer destructor returns to pool") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 256;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 0;

    void *data_ptr;
    {
        condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
        data_ptr = buf.data();
        REQUIRE(buf.owns_buffer());
    }

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 256;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 0;
    condy::ZeroCopyRxBuffer buf2 = pool.handle_finish(mock_cqe);
    REQUIRE(buf2.data() == data_ptr);
}

TEST_CASE("test zcrx - large area size") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(
        runtime, 8, condy::ZeroCopyRxArea{.size = 16ul * 4096});

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 4096;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 15ul * 4096;

    condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
    REQUIRE(buf.owns_buffer());
    REQUIRE(buf.size() == 4096);
}

TEST_CASE("test zcrx - max size buffer") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 4,
                                     condy::ZeroCopyRxArea{.size = 4096});

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 4096;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 0;

    condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
    REQUIRE(buf.owns_buffer());
    REQUIRE(buf.size() == 4096);
}

TEST_CASE("test zcrx - many buffers cycle") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(runtime, 16,
                                     condy::ZeroCopyRxArea{.size = 4096});

    for (int cycle = 0; cycle < 100; cycle++) {
        std::memset(mock_cqe, 0, sizeof(mock_cqe));
        mock_cqe->res = 256;
        reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 0;

        condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
        REQUIRE(buf.owns_buffer());
    }
}

TEST_CASE("test zcrx - handle_finish huge cqe res") {
    condy::Runtime runtime = make_zcrx_runtime();
    condy::ZeroCopyRxBufferPool pool(
        runtime, 4, condy::ZeroCopyRxArea{.size = 16ul * 4096});

    std::memset(mock_cqe, 0, sizeof(mock_cqe));
    mock_cqe->res = 65536;
    reinterpret_cast<io_uring_zcrx_cqe *>(mock_cqe + 1)->off = 0;

    condy::ZeroCopyRxBuffer buf = pool.handle_finish(mock_cqe);
    REQUIRE(buf.owns_buffer());
    REQUIRE(buf.size() == 65536);
}

#endif
