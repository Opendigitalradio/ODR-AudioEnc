#pragma once

#if HAVE_FFMPEG

#include "SampleQueue.h"
#include "common.h"
#include "InputInterface.h"

extern "C" {
#include <libavformat/avformat.h>
}

class FFMPEGInput : public InputInterface
{
    public:
        FFMPEGInput(
            const std::string& uri,
            SampleQueue<uint8_t>& queue) {

        }

        /*! Open the input interface. In case of failure, throws a
         * runtime_error.
         */
        virtual void prepare(void) {

        };

        /*! Return true if the input detected some sort of fault or
         *  abnormal termination
         */
        virtual bool fault_detected(void) {
            return true;
        };

        /*! Tell the input that it shall read from source and fill the queue.
         *  The num_samples argument is an indication on how many bytes
         *  the encoder needs.
         *  Some inputs fill the queue from another thread, in which case
         *  this function might only serve as indication that data gets
         *  consumed.
         *
         *  A return value of true means data was read, a return value of
         *  false means a normal termination of the input (e.g. end of file)
         */
        virtual bool read_source(size_t num_bytes) {
            return false;
        }
};

#endif
