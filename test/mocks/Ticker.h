#pragma once
class Ticker {
public:
    template<typename T>
    void attach(float, T) {}
    void detach() {}
};
