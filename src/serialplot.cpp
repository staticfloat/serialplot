#include <Magnum/DefaultFramebuffer.h>
#include <Magnum/Platform/Sdl2Application.h>
#include <Magnum/Buffer.h>
#include <Magnum/Mesh.h>
#include <Magnum/Math/Vector3.h>
#include <Magnum/Math/Color.h>
#include <Magnum/Shaders/VertexColor.h>
#include <Magnum/Timeline.h>
#include <Corrade/Containers/ArrayView.h>
#include <thread>
#include <map>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include "RingBuffer.hpp"

#define max(x, y) ((x) >  (y) ? (x) : (y))
#define min(x, y) ((x) <= (y) ? (x) : (y))

using namespace Magnum;
using namespace Corrade::Containers;

class Line {
    public:
        Line(unsigned int len = 1000, float r = 1.0f, float g = 1.0f, float b = 1.0f);
        ~Line();
        void drawEvent();

        void setLen(unsigned int len);
        void setColor(float r, float g, float b);
        void setColor(Color3 color);
        void pushData(float * data, unsigned int len);
        void pushData(float data);
    private:
        unsigned int len;
        float r, g, b;
        Vector3 * data;
        Mesh mesh;
        Buffer buffer;
        Shaders::VertexColor3D shader;
};

Line::Line(unsigned int len, float r, float g, float b) : data(NULL) {
    // This initial setColor just sets this->{r,g,b}, which sets setLen up for its stuff
    this->setColor(r, g, b);
    this->setLen(len);

    // Initialize the mesh as well
    this->mesh.setPrimitive(MeshPrimitive::LineStrip)
              .setCount(this->len)
              .addVertexBuffer(this->buffer, 0, Shaders::VertexColor3D::Position{}, Shaders::VertexColor3D::Color{});
}

Line::~Line() {
    delete this->data;
}

void Line::setColor(float r, float g, float b) {
    this->r = r;
    this->g = g;
    this->b = b;

    // If we already have data, then set its color!
    if( this->data != NULL ) {
        for( unsigned int i=0; i<this->len; ++i )
            this->data[2*i + 1] = {this->r, this->g, this->b};
    }
}

void Line::setColor(Color3 color) {
    this->setColor(color.r(), color.g(), color.b());
}

void Line::setLen(unsigned int len) {
    // Save old data (if we have any)
    Vector3 * old_data = this->data;
    unsigned int old_len = this->len;

    // Allocate new data and initialize color,
    this->len = len;
    this->data = new Vector3[2*this->len];
    for( unsigned int i=0; i<this->len; ++i ) {
        this->data[2*i + 0] = {i*2.0f/(this->len-1) - 1.0f, 0.0f, 0.0f};
        this->data[2*i + 1] = {this->r, this->g, this->b};
    }

    // Do we have old data that needs copying over?  If so, then DO IT
    if( old_data != NULL ) {
        // Copy the tail of the old data over
        unsigned int start_idx = max(0, old_len - this->len);
        unsigned int end_idx = min(old_len, this->len);
        for( unsigned int idx=0; idx<end_idx; ++idx )
            this->data[2*idx + 0].y() = old_data[2*(idx + start_idx) + 0].y();
        delete old_data;
    }
    this->buffer.setData(ArrayView<Vector3>(this->data, 2*this->len), BufferUsage::DynamicDraw);
    this->mesh.setCount(this->len);
}

void Line::pushData(float * data, unsigned int len) {
    // Copy old data back
    for( unsigned int i=0; i<this->len - len; ++i )
        this->data[2*i].y() = this->data[2*(i + len)].y();
    // Move new data into tail
    for( unsigned int i=0; i<len; ++i )
        this->data[2*(i + this->len - len)].y() = data[i];
}

void Line::pushData(float data) {
    this->pushData(&data, 1);
}

void Line::drawEvent() {
    // I guess the easiest way for this to work is to just setData() every single time.  :(
    this->buffer.setData(ArrayView<Vector3>(this->data, 2*this->len), BufferUsage::DynamicDraw);
    this->mesh.draw(this->shader);
}


struct SerialConfig {
    unsigned char numChannels;
    unsigned char * channelWidths;
    std::string * channelTitles;
};


class SerialPlot: public Platform::Application {
    public:
        explicit SerialPlot(const Arguments& arguments);

    private:
        void drawEvent() override;
        void keyPressEvent(KeyEvent& event) override;
        void mousePressEvent(MouseEvent& event) override;

        // Serial stuff
        bool wait_for_synchronization(void);
        const SerialConfig * read_serial_config();

        std::map<std::string, Line *> * lines;
        Timeline timeline;

        // Our serial reading thread
        void init_serial();
        void read_serial_loop();
        void cleanup_serial();
        std::thread * serialThread;
        int TTY;
        char * ttyPath;
        struct termios tty_old;
        bool should_run;
};

// This function thanks to our generous benefactors at SO:
//   http://stackoverflow.com/questions/18108932/linux-c-serial-port-reading-writing
void SerialPlot::init_serial() {
    // Open our TTY
    printf("Opening %s\n", ttyPath);
    this->TTY = open(ttyPath, O_RDWR | O_NOCTTY);

    struct termios tty;
    memset( &tty, 0, sizeof(tty) );

    // Try to get TTY attributes
    if( tcgetattr( this->TTY, &tty ) != 0 ) {
        Error() << "tcgetattr() failed";
        this->exit();
    }

    // Save old tty parameters
    this->tty_old = tty;

    // Set Baud Rate
    cfsetospeed (&tty, (speed_t)B115200);
    cfsetispeed (&tty, (speed_t)B115200);

    // Setting other Port Stuff
    tty.c_cflag     &=  ~PARENB;            // Make 8n1
    tty.c_cflag     &=  ~CSTOPB;
    tty.c_cflag     &=  ~CSIZE;
    tty.c_cflag     |=  CS8;

    tty.c_cflag     &=  ~CRTSCTS;           // no flow control
    //tty.c_cc[VMIN]   =  1;                  // read doesn't block
    //tty.c_cc[VTIME]  =  5;                  // 0.5 seconds read timeout
    tty.c_cflag     |=  CREAD | CLOCAL;     // turn on READ & ignore ctrl lines

    // Make it raw, baby!
    cfmakeraw(&tty);

    // Flush and set
    tcflush( this->TTY, TCIFLUSH );
    if( tcsetattr( this->TTY, TCSANOW, &tty ) != 0 ) {
        Error() << "tcsetattr() failed";
        this->exit();
    }
    printf("tcsetattr() succeeded!\n");

    // Start up the serial thread
    this->serialThread = new std::thread(&SerialPlot::read_serial_loop, this);
    printf("Thread started...\n");
}

void SerialPlot::cleanup_serial() {
    printf("cleaning up serial!\n");

    // Close the TTY first in order to break off any read() operations going on.
    if( tcsetattr( this->TTY, TCSANOW, &tty_old ) != 0 ) {
        Error() << "tcsetattr() failed while resetting TTY to previous state";
    }
    close(TTY);

    this->serialThread->join();
    delete this->serialThread;

    // Cleanup
    printf("cleanup done!\n");
}

bool SerialPlot::wait_for_synchronization() {
    unsigned char x0 = 0, x1 = 0, x2 = 0, x3 = 0;
    while( this->should_run ) {
        x3 = x2;
        x2 = x1;
        x1 = x0;
        if( read(this->TTY, &x0, 1) != 1 ) {
            Error() << "read() failed: " << strerror(errno) << "\n";
            break;
        }
        printf("Waiting: %d\n", x0);

        // Have we synchronized?!
        if( x0 == 0xba && x1 == 0xad && x2 == 0xf0 && x3 == 0x0d )
            return true;
    }
    return false;
}

// Our convenience macro to read or die within read_serial_config()
#define read_or_die(tty, buff, len) if( read(tty, buff, len) != len ) { \
    Error() << "read() failed: " << strerror(errno) << "\n"; \
    return NULL; \
}

const SerialConfig * SerialPlot::read_serial_config() {
    // Ask for configuration from softcore
    printf("Asking for serial config...\n");
    unsigned char cmd = 0xff;
    write(this->TTY, &cmd, 1);

    printf("Written!...\n");

    // Wait for synchronization signal from softcore
    if( !wait_for_synchronization() )
        return NULL;
    printf("Synchronized!\n");

    // Now read in config parameters!
    SerialConfig * config = new SerialConfig();
    read_or_die(this->TTY, &config->numChannels, 1);
    config->channelWidths = new unsigned char[config->numChannels];
    config->channelTitles = new std::string[config->numChannels];
    printf("numChannels: %d!\n", config->numChannels);

    // Read in each channel width
    for( int i=0; i<config->numChannels; ++i ) {
        read_or_die(this->TTY, &config->channelWidths[i], 1);
        printf("width[%d]: %d!\n", i, config->channelWidths[i]);
    }

    // Read in each channel title
    for( int i=0; i<config->numChannels; ++i ) {
        // Read in the length
        unsigned char len;
        read_or_die(this->TTY, &len, 1);

        // Read in the actual data into a char*, then shove it into the std::string
        char * temp = new char[len];
        read_or_die(this->TTY, &temp[0], len);
        config->channelTitles[i] = temp;
        printf("title[%d]: %s!\n", i, temp);
        delete temp;
    }
    return config;
}

float convert_adc_sample(char * data, unsigned int width) {
    switch(width) {
        case 1:
            return (data[0] - 128)/256.0f;
        case 2:
            return (((short *)data)[0] - 32768)/65536.0f;
        case 4:
            return (((long *)data)[0] - 2147483648)/4294967296.0f;
        default:
            return 0.0f;
    }
}

void SerialPlot::read_serial_loop() {
    // Ask for configuration, bailing out if we couldn't get one.
    const SerialConfig * config = read_serial_config();
    if( config == NULL ) {
        Error() << "Bailing from serial loop because we couldn't read a config!";
        return;
    }

    // Now that we have a configuration, let's update our map of lines:
    std::map<std::string, Line *> * lines = new std::map<std::string, Line *>();

    for( unsigned int i=0; i<config->numChannels; ++i ) {
        // Generate colors around the color wheel
        Color3 color = Color3::fromHSV(i*360.0_degf/config->numChannels, 1.0f, 1.0f);
        std::string title = config->channelTitles[i];

        // Do we already have a line with this name?  If so, just point it over
        if( this->lines->find(title) != this->lines->end() ) {
            (*lines)[title] = (*this->lines)[title];
            // We still set our own color so that we don't have color collisions!
            (*lines)[title]->setColor(color);
        } else {
            (*lines)[title] = new Line(1000, color.r(), color.g(), color.b());
        }
    }

    // Now "set" this->lines, causing our drawing code to be able to draw!
    this->lines = lines;

    // As long as you love me, I'll read your data, I'll push your data, I'll loop around
    while( this->should_run ) {
        // Iterate through each channel, reading its data, pushing it onto the respective buffers
        char data[4];
        for( unsigned int channelIdx = 0; channelIdx < config->numChannels; ++channelIdx ) {
            // Read in the sample for this channel
            if( read(this->TTY, &data[0], config->channelWidths[channelIdx]) != config->channelWidths[channelIdx] ) {
                Error() << "read() failed: " << strerror(errno) << "\n";
                break;
            }

            // Convert it to float
            float sample = convert_adc_sample(data, config->channelWidths[channelIdx]);

            // Push it onto the appropriate line
            std::string title = config->channelTitles[channelIdx];
            (*this->lines)[title]->pushData(sample);
        }
    }

    // When cleaning up, delete our config
    delete config->channelTitles;
    delete config->channelWidths;
    delete config;

    // Also cleanup lines
    while( !this->lines->empty() ) {
        delete this->lines->begin()->second;
        this->lines->erase(this->lines->begin());
    }
    delete this->lines;
    this->lines = NULL;
}

SerialPlot::SerialPlot(const Arguments& arguments):
    Platform::Application{arguments, Configuration{}.setTitle("SerialPlot v1.53b").setSize({1600, 400}).setSampleCount(8)},
    lines(NULL)
{
    if( arguments.argc <= 1 ) {
        printf("Usage: serialplot /dev/<TTY name>\n");
        std::exit(1);
    }

    // Start the time tracker thingy
    this->timeline.start();

    // Start serial reading thread
    this->ttyPath = new char[strlen(arguments.argv[1])];
    strcpy(this->ttyPath, arguments.argv[1]);
    this->should_run = true;
    init_serial();
}

void SerialPlot::drawEvent() {
    defaultFramebuffer.clear(FramebufferClear::Color);

    // Loop over lines and draw() them
    if( this->lines != NULL ) {
        for( auto line : *this->lines ) {
            line.second->drawEvent();
        }
    }

    // We keep time, I know not why
    timeline.nextFrame();
    usleep(1000);
    this->redraw();
}

void SerialPlot::keyPressEvent(KeyEvent& event) {
    switch( event.key() ) {
        case KeyEvent::Key::Space:
            this->should_run = !this->should_run;
            if( this->should_run ) {
                init_serial();
            } else {
                cleanup_serial();
            }
            break;
        default:
            return;
    }

    event.setAccepted();
}

void SerialPlot::mousePressEvent(MouseEvent& event) {
    if(event.button() == MouseEvent::Button::Left) {
    }
}



MAGNUM_APPLICATION_MAIN(SerialPlot)
