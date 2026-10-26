#pragma once

class ScopedFile {
    public:
        ScopedFile();
        ~ScopedFile();
        void foo(bool early);
};