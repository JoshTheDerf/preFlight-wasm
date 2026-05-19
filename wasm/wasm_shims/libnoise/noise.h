#ifndef LIBNOISE_NOISE_H
#define LIBNOISE_NOISE_H

namespace noise { namespace module {

class Module {
public:
    explicit Module(int sourceModuleCount = 0) : m_sourceModuleCount(sourceModuleCount) {}
    virtual ~Module() = default;

    virtual int GetSourceModuleCount() const { return m_sourceModuleCount; }
    virtual double GetValue(double x, double y, double z) const
    {
        (void)x; (void)y; (void)z;
        return 0.0;
    }

private:
    int m_sourceModuleCount;
};

class Perlin : public Module {
public:
    Perlin() : Module(0), m_frequency(1.0), m_octaveCount(1), m_persistence(0.5) {}

    void SetFrequency(double frequency) { m_frequency = frequency; }
    void SetOctaveCount(int count) { m_octaveCount = count; }
    void SetPersistence(double persistence) { m_persistence = persistence; }

    double GetValue(double x, double y, double z) const override
    {
        (void)x; (void)y; (void)z;
        return 0.0;
    }

private:
    double m_frequency;
    int    m_octaveCount;
    double m_persistence;
};

class Billow : public Module {
public:
    Billow() : Module(0), m_frequency(1.0), m_octaveCount(1), m_persistence(0.5) {}

    void SetFrequency(double frequency) { m_frequency = frequency; }
    void SetOctaveCount(int count) { m_octaveCount = count; }
    void SetPersistence(double persistence) { m_persistence = persistence; }

    double GetValue(double x, double y, double z) const override
    {
        (void)x; (void)y; (void)z;
        return 0.0;
    }

private:
    double m_frequency;
    int    m_octaveCount;
    double m_persistence;
};

class RidgedMulti : public Module {
public:
    RidgedMulti() : Module(0), m_frequency(1.0), m_octaveCount(1) {}

    void SetFrequency(double frequency) { m_frequency = frequency; }
    void SetOctaveCount(int count) { m_octaveCount = count; }

    double GetValue(double x, double y, double z) const override
    {
        (void)x; (void)y; (void)z;
        return 0.0;
    }

private:
    double m_frequency;
    int    m_octaveCount;
};

class Voronoi : public Module {
public:
    Voronoi() : Module(0), m_frequency(1.0), m_displacement(1.0) {}

    void SetFrequency(double frequency) { m_frequency = frequency; }
    void SetDisplacement(double displacement) { m_displacement = displacement; }

    double GetValue(double x, double y, double z) const override
    {
        (void)x; (void)y; (void)z;
        return 0.0;
    }

private:
    double m_frequency;
    double m_displacement;
};

}} // namespace noise::module

#endif // LIBNOISE_NOISE_H
