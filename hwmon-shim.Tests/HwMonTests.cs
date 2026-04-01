using HwMonShim;
using Xunit;

namespace HwMonShim.Tests;

public class ExtractCoreIndexTests
{
    [Theory]
    [InlineData("CPU Core #0", 0)]
    [InlineData("Core #7", 7)]
    [InlineData("Core #127", 127)]
    [InlineData("Core #0", 0)]
    [InlineData("CPU Core #15 Temperature", 15)]
    public void ValidCoreNames_ReturnsIndex(string name, int expected)
    {
        Assert.Equal(expected, HwMon.ExtractCoreIndex(name));
    }

    [Theory]
    [InlineData("Core #", -1)]
    [InlineData("Invalid", -1)]
    [InlineData("", -1)]
    [InlineData("Core #abc", -1)]
    [InlineData("No hash here", -1)]
    [InlineData("CPU Package", -1)]
    public void InvalidCoreNames_ReturnsNegativeOne(string name, int expected)
    {
        Assert.Equal(expected, HwMon.ExtractCoreIndex(name));
    }

    [Fact]
    public void MultipleHashes_UsesLastOne()
    {
        // ExtractCoreIndex uses LastIndexOf('#')
        Assert.Equal(2, HwMon.ExtractCoreIndex("Core #1 #2"));
    }
}

public class IsPackageTempTests
{
    [Theory]
    [InlineData("CPU Package")]
    [InlineData("Package")]
    [InlineData("Tctl")]
    [InlineData("Tdie")]
    [InlineData("PACKAGE Temp")]
    [InlineData("cpu package temperature")]
    public void PackageNames_ReturnsTrue(string name)
    {
        Assert.True(HwMon.IsPackageTemp(name));
    }

    [Theory]
    [InlineData("Core #0")]
    [InlineData("CPU Fan")]
    [InlineData("Temperature")]
    [InlineData("GPU")]
    [InlineData("")]
    public void NonPackageNames_ReturnsFalse(string name)
    {
        Assert.False(HwMon.IsPackageTemp(name));
    }
}

public class IsRamSensorNameTests
{
    [Theory]
    [InlineData("DIMM #1")]
    [InlineData("DRAM Temp")]
    [InlineData("Memory Temperature")]
    [InlineData("dimm")]
    [InlineData("DDR5 DIMM-TS")]
    [InlineData("MEMORY")]
    public void RamNames_ReturnsTrue(string name)
    {
        Assert.True(HwMon.IsRamSensorName(name));
    }

    [Theory]
    [InlineData("CPU Temp")]
    [InlineData("Fan Speed")]
    [InlineData("Disk")]
    [InlineData("GPU")]
    [InlineData("")]
    public void NonRamNames_ReturnsFalse(string name)
    {
        Assert.False(HwMon.IsRamSensorName(name));
    }
}
