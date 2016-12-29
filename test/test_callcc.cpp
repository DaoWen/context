
//          Copyright Oliver Kowalke 2009.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <boost/array.hpp>
#include <boost/assert.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/utility.hpp>
#include <boost/variant.hpp>

#include <boost/context/continuation.hpp>
#include <boost/context/detail/config.hpp>

#ifdef BOOST_WINDOWS
#include <windows.h>
#endif

#if defined(BOOST_MSVC)
# pragma warning(push)
# pragma warning(disable: 4723)
#endif

typedef boost::variant<int,std::string> variant_t;

namespace ctx = boost::context;

int value1 = 0;
std::string value2;
double value3 = 0.;

struct X {
    ctx::continuation foo( ctx::continuation && c) {
        value1 = ctx::data< int >( c);
        return std::move( c);
    }
};

struct Y {
    Y() {
        value1 = 3;
    }

    Y( Y const&) = delete;
    Y & operator=( Y const&) = delete;

    ~Y() {
        value1 = 7;
    }
};

class moveable {
public:
    bool    state;
    int     value;

    moveable() :
        state( false),
        value( -1) {
    }

    moveable( int v) :
        state( true),
        value( v) {
    }

    moveable( moveable && other) :
        state( other.state),
        value( other.value) {
        other.state = false;
        other.value = -1;
    }

    moveable & operator=( moveable && other) {
        if ( this == & other) return * this;
        state = other.state;
        value = other.value;
        other.state = false;
        other.value = -1;
        return * this;
    }

    moveable( moveable const& other) = delete;
    moveable & operator=( moveable const& other) = delete;

    void operator()() {
        value1 = value;
    }
};

struct my_exception : public std::runtime_error {
    ctx::continuation   c;
    my_exception( ctx::continuation && c_, char const* what) :
        std::runtime_error( what),
        c{ std::move( c_) } {
    }
};

#ifdef BOOST_MSVC
void seh( bool & catched) {
    __try {
        int i = 1;
        i /= 0;
    } __except( EXCEPTION_EXECUTE_HANDLER) {
        catched = true;
    }
}
#endif

ctx::continuation fn1( ctx::continuation && c) {
    value1 = ctx::data< int >( c);
    return std::move( c);
}

ctx::continuation fn2( ctx::continuation && c) {
    try {
        throw std::runtime_error( ctx::data< const char * >( c) );
    } catch ( std::runtime_error const& e) {
        value2 = e.what();
    }
    return std::move( c);
}

ctx::continuation fn3( ctx::continuation && c) {
    double d = ctx::data< double >( c);
    d += 3.45;
    value3 = d;
    return std::move( c);
}

ctx::continuation fn5( ctx::continuation && c) {
    value1 = 3;
    return std::move( c);
}

ctx::continuation fn4( ctx::continuation && c) {
    ctx::continuation c1 = ctx::callcc( fn5);
    value3 = 3.14;
    return std::move( c);
}

ctx::continuation fn6( ctx::continuation && c) {
    try {
        value1 = 3;
        c = ctx::callcc( std::move( c) );
        value1 = 7;
        c = ctx::callcc( std::move( c) );
    } catch ( my_exception & e) {
        value2 = e.what();
    }
    return std::move( c);
}

ctx::continuation fn7( ctx::continuation && c) {
    Y y;
    return ctx::callcc( std::move( c) );
}

ctx::continuation fn8( ctx::continuation && c) {
    value1 = ctx::data< int >( c);
    return std::move( c);
}

ctx::continuation fn9( ctx::continuation && c) {
    value1 = ctx::data< int >( c);
    c = ctx::callcc( std::move( c), value1);
    value1 = ctx::data< int >( c);
    return std::move( c);
}

ctx::continuation fn10( ctx::continuation && c) {
    int & i = ctx::data< int & >( c);
    return ctx::callcc( std::move( c), std::ref( i) );
}

ctx::continuation fn11( ctx::continuation && c) {
    moveable m = ctx::data< moveable >( c);
    c = ctx::callcc( std::move( c), std::move( m) );
    m = ctx::data< moveable >( c);
    return ctx::callcc( std::move( c), std::move( m) );
}

ctx::continuation fn12( ctx::continuation && c) {
    int i; std::string str;
    std::tie( i, str) = ctx::data< int, std::string >( c);
    return ctx::callcc( std::move( c), i, str);
}

ctx::continuation fn13( ctx::continuation && c) {
    int i; moveable m;
    std::tie( i, m) = ctx::data< int, moveable >( c);
    return ctx::callcc( std::move( c), i, std::move( m) );
}

ctx::continuation fn14( ctx::continuation && c) {
    variant_t data = ctx::data< variant_t >( c);
    int i = boost::get< int >( data);
    data = boost::lexical_cast< std::string >( i);
    return ctx::callcc( std::move( c), data);
}

ctx::continuation fn15( ctx::continuation && c) {
    Y * py = ctx::data< Y * >( c);
    return ctx::callcc( std::move( c), py);
}

ctx::continuation fn16( ctx::continuation && c) {
    int i = ctx::data< int >( c);
    value1 = i;
    c = ctx::callcc( std::move( c), i);
    value1 = ctx::data< int >( c);
    return std::move( c);
}

ctx::continuation fn17( ctx::continuation && c) {
    int i; int j;
    std::tie( i, j) = ctx::data< int, int >( c);
    for (;;) {
        c = ctx::callcc( std::move( c), i, j);
        std::tie( i, j) = ctx::data< int, int >( c);
    }
    return std::move( c);
}


void test_move() {
    value1 = 0;
    ctx::continuation c;
    BOOST_CHECK( ! c );
    ctx::continuation c1 = ctx::callcc( fn9, 1);
    ctx::continuation c2 = ctx::callcc( fn9, 3);
    BOOST_CHECK( c1 );
    BOOST_CHECK( c2 );
    c1 = std::move( c2);
    BOOST_CHECK( c1 );
    BOOST_CHECK( ! c2 );
    BOOST_CHECK_EQUAL( 3, value1);
    ctx::callcc( std::move( c1), 0);
    BOOST_CHECK_EQUAL( 0, value1);
    BOOST_CHECK( ! c1 );
    BOOST_CHECK( ! c2 );
}

void test_bind() {
    value1 = 0;
    X x;
    ctx::continuation c = ctx::callcc( std::bind( & X::foo, x, std::placeholders::_1), 7);
    BOOST_CHECK_EQUAL( 7, value1);
}

void test_exception() {
    {
        const char * what = "hello world";
        ctx::continuation c = ctx::callcc( fn2, what);
        BOOST_CHECK_EQUAL( std::string( what), value2);
        BOOST_CHECK( ! c );
    }
#ifdef BOOST_MSVC
    {
        bool catched = false;
        std::thread([&catched](){
                ctx::continuation c = ctx::callcc([&catched](ctx::continuation && c){
                c = ctx::callcc( std::move( c) );
                            seh( catched);
                            return std::move( c);
                        });
            BOOST_CHECK( c );
            ctx::callcc( std::move( c) );
        }).join();
        BOOST_CHECK( catched);
    }
#endif
}

void test_fp() {
    double d = 7.13;
    ctx::continuation c = ctx::callcc( fn3, d);
    BOOST_CHECK_EQUAL( 10.58, value3);
    BOOST_CHECK( ! c );
}

void test_stacked() {
    value1 = 0;
    value3 = 0.;
    ctx::continuation c = ctx::callcc( fn4);
    BOOST_CHECK_EQUAL( 3, value1);
    BOOST_CHECK_EQUAL( 3.14, value3);
    BOOST_CHECK( ! c );
}

void test_prealloc() {
    value1 = 0;
    ctx::default_stack alloc;
    ctx::stack_context sctx( alloc.allocate() );
    void * sp = static_cast< char * >( sctx.sp) - 10;
    std::size_t size = sctx.size - 10;
    ctx::continuation c = ctx::callcc( std::allocator_arg, ctx::preallocated( sp, size, sctx), alloc, fn1, 7);
    BOOST_CHECK_EQUAL( 7, value1);
    BOOST_CHECK( ! c );
}

void test_ontop() {
    {
        int i = 3, j = 0;
        ctx::continuation c = ctx::callcc([](ctx::continuation && c) {
                    int x = ctx::data< int >( c);
                    for (;;) {
                        c = ctx::callcc( std::move( c), x*10);
                        if ( ctx::has_data( c) ) {
                            x = ctx::data< int >( c);
                        }
                    }
                    return std::move( c);
                }, i);
        c = ctx::callcc(
                std::move( c),
                ctx::exec_ontop_arg,
                [](ctx::continuation & c){
                    int x = ctx::data< int >( c);
                    return x-10;
                },
                i);
        if ( ctx::has_data( c) ) {
            j = ctx::data< int >( c);
        }
        BOOST_CHECK( c );
        BOOST_CHECK_EQUAL( j, -70);
    }
    {
        int i = 3, j = 1;
        ctx::continuation c;
        c = ctx::callcc( fn17, i, j);
        std::tie( i, j) = ctx::data< int, int >( c);
        c = ctx::callcc(
                std::move( c),
                ctx::exec_ontop_arg,
                [](ctx::continuation & c){
                    int x, y;
                    std::tie( x, y) = ctx::data< int, int >( c);
                    return std::make_tuple( x - y, x + y);
                },
                i, j);
        std::tie( i, j) = ctx::data< int, int >( c);
        BOOST_CHECK_EQUAL( i, 2);
        BOOST_CHECK_EQUAL( j, 4);
    }
    {
        moveable m1( 7), m2, dummy;
        ctx::continuation c =  ctx::callcc( fn11, std::move( dummy) );
        BOOST_CHECK( 7 == m1.value);
        BOOST_CHECK( m1.state);
        BOOST_CHECK( -1 == m2.value);
        BOOST_CHECK( ! m2.state);
        c = ctx::callcc( std::move( c),
                         ctx::exec_ontop_arg,
                         [](ctx::continuation & c){
                             moveable m = ctx::data< moveable >( c);
                             BOOST_CHECK( m.state);
                             BOOST_CHECK( 7 == m.value);
                             return std::move( m);
                         },
                         std::move( m1) );
        m2 = ctx::data< moveable >( c);
        BOOST_CHECK( ! m1.state);
        BOOST_CHECK( -1 == m1.value);
        BOOST_CHECK( m2.state);
        BOOST_CHECK( 7 == m2.value);
    }
}

void test_ontop_exception() {
    {
        value1 = 0;
        value2 = "";
        ctx::continuation c = ctx::callcc([](ctx::continuation && c){
                for (;;) {
                    value1 = 3;
                    try {
                        c = ctx::callcc( std::move( c) );
                    } catch ( my_exception & ex) {
                        value2 = ex.what();
                        return std::move( ex.c); 
                    }
                }
                return std::move( c);
        });
        c = ctx::callcc( std::move( c) );
        BOOST_CHECK_EQUAL( 3, value1);
        const char * what = "hello world";
        ctx::callcc(
                std::move( c),
                ctx::exec_ontop_arg,
                [what](ctx::continuation & c){
                    throw my_exception( std::move( c), what);
                });
        BOOST_CHECK_EQUAL( 3, value1);
        BOOST_CHECK_EQUAL( std::string( what), value2);
    }
    {
        value2 = "";
        int i = 3, j = 1;
        ctx::continuation c = ctx::callcc([]( ctx::continuation && c) {
                int x; int y;
                std::tie( x, y) = ctx::data< int, int >( c);
                for (;;) {
                    try {
                        c = ctx::callcc( std::move( c), x+y,x-y);
                        std::tie( x, y) = ctx::data< int, int >( c);
                    } catch ( my_exception & ex) {
                        value2 = ex.what();
                        return std::move( ex.c); 
                    }
                }
                return std::move( c);
            },
            i, j);
        BOOST_CHECK( c );
        std::tie( i, j) = ctx::data< int, int >( c);
        BOOST_CHECK_EQUAL( i, 4);
        BOOST_CHECK_EQUAL( j, 2);
        char const * what = "hello world";
        c = ctx::callcc(
                std::move( c),
                ctx::exec_ontop_arg,
                [](ctx::continuation & c) {
                    char const * what = ctx::data< char const * >( c);
                    throw my_exception( std::move( c), what);
                    return what;
                },
                what);
        BOOST_CHECK( ! c );
        BOOST_CHECK_EQUAL( i, 4);
        BOOST_CHECK_EQUAL( j, 2);
        BOOST_CHECK_EQUAL( std::string( what), value2);
    }
}

void test_termination() {
    {
        value1 = 0;
        ctx::continuation c = ctx::callcc( fn7);
        BOOST_CHECK_EQUAL( 3, value1);
    }
    BOOST_CHECK_EQUAL( 7, value1);
    {
        value1 = 0;
        BOOST_CHECK_EQUAL( 0, value1);
        ctx::continuation c = ctx::callcc( fn5);
        BOOST_CHECK_EQUAL( 3, value1);
        BOOST_CHECK( ! c );
    }
    {
        value1 = 0;
        BOOST_CHECK_EQUAL( 0, value1);
        int i = 3;
        ctx::continuation c;
        BOOST_CHECK( ! c );
        c = ctx::callcc( fn9, i);
        BOOST_CHECK( c );
        i = ctx::data< int >( c);
        BOOST_CHECK_EQUAL( i, value1);
        BOOST_CHECK( c );
        i = 7;
        c = ctx::callcc( std::move( c), i);
        BOOST_CHECK( ! c );
        BOOST_CHECK_EQUAL( i, value1);
    }
}

void test_one_arg() {
    {
        value1 = 0;
        ctx::continuation c;
        c = ctx::callcc( fn8, 7);
        BOOST_CHECK_EQUAL( 7, value1);
    }
    {
        int i = 3, j = 0;
        ctx::continuation c;
        c = ctx::callcc( fn9, i);
        j = ctx::data< int >( c);
        BOOST_CHECK_EQUAL( i, j);
    }
    {
        int i_ = 3, j_ = 7;
        int & i = i_;
        int & j = j_;
        BOOST_CHECK_EQUAL( i, i_);
        BOOST_CHECK_EQUAL( j, j_);
        BOOST_CHECK( & i != & j);
        ctx::continuation c;
        c = ctx::callcc( fn10, std::ref( i) );
        BOOST_CHECK( ctx::has_data( c) );
        j = ctx::data< int & >( c);
        BOOST_CHECK_EQUAL( i, i_);
        BOOST_CHECK_EQUAL( j, i_);
    }
    {
        Y y;
        Y * py = nullptr;
        ctx::continuation c;
        c = ctx::callcc( fn15, & y);
        py = ctx::data< Y * >( c);
        BOOST_CHECK( py == & y);
    }
    {
        moveable m1( 7), m2;
        BOOST_CHECK( 7 == m1.value);
        BOOST_CHECK( m1.state);
        BOOST_CHECK( -1 == m2.value);
        BOOST_CHECK( ! m2.state);
        ctx::continuation c;
        c = ctx::callcc( fn11, std::move( m1) );
        m2 = ctx::data< moveable >( c);
        BOOST_CHECK( -1 == m1.value);
        BOOST_CHECK( ! m1.state);
        BOOST_CHECK( 7 == m2.value);
        BOOST_CHECK( m2.state);
    }
}

void test_two_args() {
    {
        int i1 = 3, i2 = 0;
        std::string str1("abc"), str2;
        ctx::continuation c = ctx::callcc( fn12, i1, str1);
        std::tie( i2, str2) = ctx::data< int, std::string >( c);
        BOOST_CHECK_EQUAL( i1, i2);
        BOOST_CHECK_EQUAL( str1, str2);
    }
    {
        int i1 = 3, i2 = 0;
        moveable m1( 7), m2;
        BOOST_CHECK( 7 == m1.value);
        BOOST_CHECK( m1.state);
        BOOST_CHECK( -1 == m2.value);
        BOOST_CHECK( ! m2.state);
        ctx::continuation c;
        c = ctx::callcc( fn13, i1, std::move( m1) );
        std::tie( i2, m2) = ctx::data< int, moveable >( c);
        BOOST_CHECK_EQUAL( i1, i2);
        BOOST_CHECK( -1 == m1.value);
        BOOST_CHECK( ! m1.state);
        BOOST_CHECK( 7 == m2.value);
        BOOST_CHECK( m2.state);
    }
}

void test_variant() {
    {
        int i = 7;
        variant_t data1 = i, data2;
        ctx::continuation c;
        c = ctx::callcc( fn14, data1);
        data2 = ctx::data< variant_t >( c);
        std::string str = boost::get< std::string >( data2);
        BOOST_CHECK_EQUAL( std::string("7"), str);
    }
}

#ifdef BOOST_WINDOWS
void test_bug12215() {
        ctx::continuation c = ctx::callcc(
            [](ctx::continuation && c) {
                char buffer[MAX_PATH];
                GetModuleFileName( nullptr, buffer, MAX_PATH);
                return std::move( c);
            });
}
#endif

boost::unit_test::test_suite * init_unit_test_suite( int, char* [])
{
    boost::unit_test::test_suite * test =
        BOOST_TEST_SUITE("Boost.Context: callcc test suite");

    test->add( BOOST_TEST_CASE( & test_move) );
    test->add( BOOST_TEST_CASE( & test_bind) );
    test->add( BOOST_TEST_CASE( & test_exception) );
    test->add( BOOST_TEST_CASE( & test_fp) );
    test->add( BOOST_TEST_CASE( & test_stacked) );
    test->add( BOOST_TEST_CASE( & test_stacked) );
    test->add( BOOST_TEST_CASE( & test_prealloc) );
    test->add( BOOST_TEST_CASE( & test_ontop) );
    test->add( BOOST_TEST_CASE( & test_ontop_exception) );
    test->add( BOOST_TEST_CASE( & test_termination) );
    test->add( BOOST_TEST_CASE( & test_one_arg) );
    test->add( BOOST_TEST_CASE( & test_two_args) );
    test->add( BOOST_TEST_CASE( & test_variant) );
#ifdef BOOST_WINDOWS
    test->add( BOOST_TEST_CASE( & test_bug12215) );
#endif

    return test;
}

#if defined(BOOST_MSVC)
# pragma warning(pop)
#endif
