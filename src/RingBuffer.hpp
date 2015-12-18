template <typename T>
class RingBuffer {
public:
    // Initialize the RingBuffer with all its constituent memory locations
    RingBuffer( const unsigned int len ) {
        this->data = new T[len];
        for( unsigned int i=0; i<len; ++i )
            this->data[i] = 0;
        this->readIdx = 0;
        this->writeIdx = 0;
        this->last_writeIdx = 0;
        this->last_readIdx = 0;
        this->len = len;
    }

    // Cleanup our heap-allocated memory
    ~RingBuffer() {
        delete[] this->data;
    }

    // Return the number of samples writable to this buffer
    unsigned int writable() {
        int readIdx_adjusted = readIdx;

        // Note that this is <= because if the two indexes are equal, we can write!
        if( readIdx_adjusted <= writeIdx )
            readIdx_adjusted += len;
        return readIdx_adjusted - writeIdx;
    }

    // Return the number of samples readable from this buffer
    unsigned int readable() {
        int writeIdx_adjusted = writeIdx;

        if( writeIdx_adjusted < readIdx )
            writeIdx_adjusted += len;
        return writeIdx_adjusted - readIdx;
    }

    // Only return true if readIndex is at least num_samples ahead of writeIndex
    bool writable( const unsigned int num_samples ) {
        return writable() > num_samples;
    }

    // Only return true if writeIndex is at least num_samples ahead of readIndex
    bool readable( const unsigned int num_samples ) {
        return readable() > num_samples;
    }

    // Read num_samples of data into outputBuff, returns true on success
    bool read( const unsigned int num_samples, T * outputBuff ) {
        if( !readable(num_samples) ) {
            //printf("[%d] Won't read %d samples from ringbuffer, (W: %d, R: %d)\n", packet_count, num_samples, writeIdx, readIdx);
            return false;
        }

        // If we have to wraparound to service this request, then do so by invoking ourselves twice
        if( readIdx + num_samples > len ) {
            unsigned int first_batch = len - readIdx;
            read(first_batch, outputBuff);
            read(num_samples - first_batch, outputBuff + first_batch);
        } else {
            // Write data out, also set data to zero in our wake
            for( unsigned int i=0; i<num_samples; ++i ) {
                outputBuff[i] = this->data[readIdx + i];
                this->data[readIdx + i] = 0;
            }
            readIdx = (readIdx + num_samples)%len;
        }
        return true;
    }

    // Write num_samples of data from outputBuff, returns true on success
    bool write( const unsigned int num_samples, const T * inputBuff ) {
        if( !writable(num_samples) ) {
            //printf("[%d] Won't write %d samples to ringbuffer, (W: %d, R: %d)\n", packet_count, num_samples, writeIdx, readIdx);
            return false;
        }

        if( writeIdx + num_samples > len ) {
            unsigned int first_batch = len - writeIdx;
            write(first_batch, inputBuff);
            write(num_samples - first_batch, inputBuff + first_batch);
        } else {
            // Copy data in
            for( unsigned int i=0; i<num_samples; ++i )
                this->data[writeIdx + i] = inputBuff[i];
            writeIdx = (writeIdx + num_samples)%len;
        }
        return true;
    }

    // Return the amount written into the ringbuffer since the last time we asked about it!
    int getAmountWritten() {
        int amnt = this->last_writeIdx - this->writeIdx;
        if( amnt < 0 )
            amnt += this->len;
        this->last_writeIdx = this->writeIdx;
        return amnt;
    }

    // Return the amount read from the ringbuffer since the last time we asked about it!
    int getAmountRead() {
        int amnt = this->last_readIdx - this->readIdx;
        if( amnt < 0 )
            amnt += this->len;
        this->last_readIdx = this->readIdx;
        return amnt;
    }

protected:
    T * data;
    unsigned int readIdx, writeIdx, len;
    unsigned int last_writeIdx, last_readIdx;
};
