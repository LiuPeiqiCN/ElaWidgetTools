#include "ElaDxgiManager.h"

#include <QDateTime>
#include <QDebug>
#include <QPainterPath>
#include <QUrl>

#include "private/ElaDxgiPrivate.h"
#include "qpainter.h"
Q_PROPERTY_CREATE_Q_CPP(ElaDxgi, QStringList, DxDeviceList)
Q_PROPERTY_CREATE_Q_CPP(ElaDxgi, QStringList, OutputDeviceList)
Q_PROPERTY_CREATE_Q_CPP(ElaDxgi, int, DxDeviceID);
Q_PROPERTY_CREATE_Q_CPP(ElaDxgi, int, OutputDeviceID);
Q_PROPERTY_CREATE_Q_CPP(ElaDxgi, QString, LastError)
Q_PROPERTY_CREATE_Q_CPP(ElaDxgi, bool, IsGrabActive)
Q_PROPERTY_CREATE_Q_CPP(ElaDxgi, QRect, GrabArea);
Q_PROPERTY_CREATE_Q_CPP(ElaDxgi, int, GrabFrameRate);  // 截图帧数
Q_PROPERTY_CREATE_Q_CPP(ElaDxgi, int, TimeoutMsValue); // 超时等待

ElaDxgi::ElaDxgi(QObject* parent)
    : QObject(parent), d_ptr(new ElaDxgiPrivate())
{
    Q_D(ElaDxgi);
    d->q_ptr = this;
    d->_pIsGrabActive = false;
    d->_pGrabFrameRate = 120;
    d->_pTimeoutMsValue = 50;
}

ElaDxgi::~ElaDxgi()
{
    releaseInterface();
}

bool ElaDxgi::initialize(int dxID, int outputID)
{
    Q_D(ElaDxgi);
    d->_pDxDeviceID = dxID;
    d->_pOutputDeviceID = outputID;
    releaseInterface();
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dContext = nullptr;
    D3D_FEATURE_LEVEL feat = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                                   D3D11_SDK_VERSION, &d3dDevice, &feat, &d3dContext);
    if (FAILED(hr))
    {
        d->_pLastError = "Failed to D3D11CreateDevice ErrorCode = " + QString::number(uint(hr), 16);
        qDebug() << d->_pLastError;
        if (d3dDevice)
        {
            d3dDevice->Release();
        }
        if (d3dContext)
        {
            d3dContext->Release();
        }
        return false;
    }

    IDXGIFactory* dxgiFactory = nullptr;
    CreateDXGIFactory(__uuidof(dxgiFactory), reinterpret_cast<void**>(&dxgiFactory));
    IDXGIAdapter* dxgiAdapter = nullptr;
    QVector<IDXGIAdapter*> dxgiAdapterVector;
    for (uint i = 0; dxgiFactory->EnumAdapters(i, &dxgiAdapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        dxgiAdapterVector.append(dxgiAdapter);
    }
    dxgiFactory->Release();
    dxgiAdapter = nullptr;
    d->_pDxDeviceList.clear();
    for (int i = 0; i < dxgiAdapterVector.count(); i++)
    {
        IDXGIAdapter* adapt = dxgiAdapterVector.at(i);
        DXGI_ADAPTER_DESC desc;
        adapt->GetDesc(&desc);
        d->_pDxDeviceList.append(QString::fromWCharArray(desc.Description));
    }
    if (dxID >= 0 && dxID < dxgiAdapterVector.count())
    {
        dxgiAdapter = dxgiAdapterVector.at(dxID);
    }

    if (!dxgiAdapter)
    {
        d->_pLastError = "Failed to found gpu";
        qDebug() << d->_pLastError;
        d3dDevice->Release();
        d3dContext->Release();
        for (auto adapter : dxgiAdapterVector)
        {
            adapter->Release();
        }
        return false;
    }

    IDXGIOutput* dxgiOutput = nullptr;
    QVector<IDXGIOutput*> dxgiOutputVector;
    for (uint i = 0; dxgiAdapter->EnumOutputs(i, &dxgiOutput) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        dxgiOutputVector.append(dxgiOutput);
    }
    for (auto adapter : dxgiAdapterVector)
    {
        adapter->Release();
    }
    dxgiOutput = nullptr;
    d->_pOutputDeviceList.clear();
    for (int i = 0; i < dxgiOutputVector.count(); i++)
    {
        const auto& output = dxgiOutputVector.at(i);
        DXGI_OUTPUT_DESC desc;
        output->GetDesc(&desc);
        d->_pOutputDeviceList.append(QString::fromWCharArray(desc.DeviceName));
    }
    if (outputID >= 0 && outputID < dxgiOutputVector.count())
    {
        dxgiOutput = dxgiOutputVector.at(outputID);
    }

    if (!dxgiOutput)
    {
        d->_pLastError = "Failed to found screen!";
        qDebug() << d->_pLastError;
        d3dDevice->Release();
        d3dContext->Release();
        for (auto output : dxgiOutputVector)
        {
            output->Release();
        }
        return false;
    }
    IDXGIOutput6* dxgiOutput6 = nullptr;
    hr = dxgiOutput->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&dxgiOutput6));
    for (auto output : dxgiOutputVector)
    {
        output->Release();
    }
    if (FAILED(hr))
    {
        d->_pLastError = "Failed to QueryInterface IDXGIOutput6 ErrorCode = " + QString::number(uint(hr), 16);
        qDebug() << d->_pLastError;
        d3dDevice->Release();
        d3dContext->Release();
        dxgiOutput6->Release();
        return false;
    }
    UINT supportedFormatsCount = 1; // 支持的格式数
    DXGI_FORMAT supportedFormats[1] = {DXGI_FORMAT_B8G8R8A8_UNORM};
    hr = dxgiOutput6->DuplicateOutput1(d3dDevice, 0, supportedFormatsCount, supportedFormats, &d->_duplication);
    dxgiOutput6->Release();
    if (FAILED(hr))
    {
        d->_pLastError = "Failed to DuplicateOutput ErrorCode = " + QString::number(uint(hr), 16);
        qDebug() << d->_pLastError;
        d3dDevice->Release();
        d3dContext->Release();
        if (d->_duplication)
        {
            d->_duplication->Release();
            d->_duplication = nullptr;
        }
        return false;
    }
    d->_device = d3dDevice;
    d->_context = d3dContext;
    return true;
}

QImage ElaDxgi::getGrabImage() const
{
    return d_ptr->_grabImg;
}

void ElaDxgi::onGrabScreen()
{
    Q_D(ElaDxgi);
    qreal startTime = 0;
    qreal endTime = 0;
    if (!d->_duplication || !d->_device || !d->_context)
    {
        return;
    }
    while (d->_pIsGrabActive)
    {
        IDXGIResource* desktopRes = nullptr;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;
        while (true)
        {
            startTime = QDateTime::currentMSecsSinceEpoch();
            d->_duplication->ReleaseFrame();
            HRESULT hr = d->_duplication->AcquireNextFrame(d->_pTimeoutMsValue, &frameInfo, &desktopRes);
            if (FAILED(hr))
            {
                if (hr != DXGI_ERROR_WAIT_TIMEOUT)
                {
                    if (desktopRes)
                    {
                        desktopRes->Release();
                        desktopRes = nullptr;
                    }
                    while (1)
                    {
                        if (initialize(d->_pDxDeviceID, d->_pOutputDeviceID))
                        {
                            break;
                        }
                    }
                }
                continue;
            }
            if (frameInfo.LastPresentTime.QuadPart)
            {
                break;
            }
        }
        D3D11_TEXTURE2D_DESC desc;
        ID3D11Texture2D* textrueRes = nullptr;
        HRESULT hr = desktopRes->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&textrueRes));
        desktopRes->Release();
        if (FAILED(hr))
        {
            qDebug() << "Failed to ID3D11Texture2D result =" << QString::number(uint(hr), 16);
            continue;
        }
        textrueRes->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC texDesc;
        ZeroMemory(&texDesc, sizeof(texDesc));
        texDesc.Width = desc.Width;
        texDesc.Height = desc.Height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.SampleDesc.Count = 1;
        texDesc.SampleDesc.Quality = 0;
        texDesc.Usage = D3D11_USAGE_STAGING;
        texDesc.Format = desc.Format;
        texDesc.BindFlags = 0;
        texDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        texDesc.MiscFlags = 0;
        d->_device->CreateTexture2D(&texDesc, nullptr, &d->_texture);
        d->_context->CopyResource(d->_texture, textrueRes);
        IDXGISurface1* surface = nullptr;
        hr = d->_texture->QueryInterface(__uuidof(IDXGISurface1), reinterpret_cast<void**>(&surface));
        if (FAILED(hr))
        {
            qDebug() << "Failed to QueryInterface IDXGISurface1 ErrorCode ="
                     << QString::number(uint(hr), 16);
            continue;
        }

        DXGI_MAPPED_RECT map;
        surface->Map(&map, DXGI_MAP_READ);
        QImage grabImage(static_cast<uchar*>(map.pBits), int(desc.Width), int(desc.Height),
                         QImage::Format_ARGB32);
        surface->Unmap();
        surface->Release();
        d->_texture->Release();
        d->_grabImg = grabImage.copy(d->_pGrabArea);
        Q_EMIT grabScreenOver();
        endTime = QDateTime::currentMSecsSinceEpoch();
        if (d->_lastGrabTime == 0)
        {
            d->_lastGrabTime = endTime - startTime; // 毫秒
        }
        else
        {
            d->_lastGrabTime = ((endTime - startTime) + d->_lastGrabTime) / 2.0;
        }
        d->_cpuSleepTime = (1000 - d->_lastGrabTime * d->_pGrabFrameRate) / d->_pGrabFrameRate;
        cpuSleep(d->_cpuSleepTime * 1000);
        // qDebug() << _cpuSleepTime << _lastGrabTime;
    }
}

void ElaDxgi::releaseInterface()
{
    Q_D(ElaDxgi);
    if (d->_duplication)
    {
        d->_duplication->Release();
        d->_duplication = nullptr;
    }
    if (d->_device)
    {
        d->_device->Release();
        d->_device = nullptr;
    }
    if (d->_context)
    {
        d->_context->Release();
        d->_context = nullptr;
    }
}

void ElaDxgi::cpuSleep(int usec)
{
    if (usec <= 0)
    {
        return;
    }
    LARGE_INTEGER cpuFerq;
    LARGE_INTEGER startTime;
    LARGE_INTEGER endTime;
    QueryPerformanceFrequency(&cpuFerq);
    QueryPerformanceCounter(&startTime);
    while (1)
    {
        QueryPerformanceCounter(&endTime);
        if (((endTime.QuadPart - startTime.QuadPart) * 1000000) / cpuFerq.QuadPart > usec)
        {
            break;
        }
    }
}

ElaDxgiManager::ElaDxgiManager(QObject* parent)
    : QObject{parent}
{
    _dxgiThread = new QThread(this);
    _dxgi = new ElaDxgi();
    setGrabArea(0, 0, GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
    bool ret = _dxgi->initialize(0, 0);
    if (!ret)
    {
        for (int i = 1; i < _dxgi->getDxDeviceList().count(); i++)
        {
            bool retAgain = _dxgi->initialize(i, 0);
            if (retAgain)
            {
                break;
            }
        }
    }
    if (!_dxgi)
    {
        qCritical() << "No available screenshot devices";
        return;
    }
    _dxgi->moveToThread(_dxgiThread);
    _dxgiThread->start();
    connect(this, &ElaDxgiManager::grabScreen, _dxgi, &ElaDxgi::onGrabScreen);
    connect(_dxgi, &ElaDxgi::grabScreenOver, this, &ElaDxgiManager::sendUpdate);
}

ElaDxgiManager::~ElaDxgiManager()
{
    if (_dxgi)
    {
        _dxgi->setIsGrabActive(false);
    }
    if (_dxgiThread->isRunning())
    {
        _dxgiThread->quit();
        _dxgiThread->wait();
    }
    delete this->_dxgi;
}

QStringList ElaDxgiManager::getDxDeviceList() const
{
    return _dxgi->getDxDeviceList();
}

QStringList ElaDxgiManager::getOutputDeviceList() const
{
    return _dxgi->getOutputDeviceList();
}

QImage ElaDxgiManager::grabScreenToImage() const
{
    return _dxgi->getGrabImage();
}

void ElaDxgiManager::startGrabScreen()
{
    if (!_dxgi->getIsGrabActive())
    {
        _dxgi->setIsGrabActive(true);
        Q_EMIT grabScreen();
    }
}

void ElaDxgiManager::stopGrabScreen()
{
    _dxgi->setIsGrabActive(false);
}

bool ElaDxgiManager::setDxDeviceID(int dxID)
{
    if (dxID < 0 || _dxgi->getDxDeviceList().count() <= dxID)
    {
        return false;
    }
    if (_dxgi->getIsGrabActive())
    {
        _dxgi->setIsGrabActive(false);
        _dxgi->initialize(dxID, _dxgi->getOutputDeviceID());
        _dxgi->setIsGrabActive(true);
        Q_EMIT grabScreen();
    }
    else
    {
        _dxgi->initialize(dxID, _dxgi->getOutputDeviceID());
    }
    return true;
}

int ElaDxgiManager::getDxDeviceID() const
{
    return this->_dxgi->getDxDeviceID();
}

bool ElaDxgiManager::setOutputDeviceID(int deviceID)
{
    if (deviceID < 0 || _dxgi->getOutputDeviceList().count() <= deviceID)
    {
        return false;
    }
    if (_dxgi->getIsGrabActive())
    {
        _dxgi->setIsGrabActive(false);
        _dxgi->initialize(_dxgi->getDxDeviceID(), deviceID);
        _dxgi->setIsGrabActive(true);
        Q_EMIT grabScreen();
    }
    else
    {
        _dxgi->initialize(_dxgi->getDxDeviceID(), deviceID);
    }
    return true;
}

int ElaDxgiManager::getOutputDeviceID() const
{
    return this->_dxgi->getOutputDeviceID();
}

void ElaDxgiManager::setGrabArea(int width, int height)
{
    int maxWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int maxHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || width > maxWidth)
    {
        width = maxWidth;
    }
    if (height <= 0 || height > maxHeight)
    {
        height = maxHeight;
    }
    _dxgi->setGrabArea(QRect((maxWidth - width) / 2, (maxHeight - height) / 2, width, height));
}

void ElaDxgiManager::setGrabArea(int x, int y, int width, int height)
{
    int maxWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int maxHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || width > maxWidth)
    {
        width = maxWidth;
    }
    if (height <= 0 || height > maxHeight)
    {
        height = maxHeight;
    }
    _dxgi->setGrabArea(QRect(x, y, width, height));
}

QRect ElaDxgiManager::getGrabArea() const
{
    return _dxgi->getGrabArea();
}

void ElaDxgiManager::setGrabFrameRate(int frameRateValue)
{
    if (frameRateValue > 0)
    {
        this->_dxgi->setGrabFrameRate(frameRateValue);
    }
}

int ElaDxgiManager::getGrabFrameRate() const
{
    return this->_dxgi->getGrabFrameRate();
}

void ElaDxgiManager::setTimeoutMsValue(int timeoutValue)
{
    if (timeoutValue > 0)
    {
        this->_dxgi->setTimeoutMsValue(timeoutValue);
    }
}

int ElaDxgiManager::getTimeoutMsValue() const
{
    return this->_dxgi->getTimeoutMsValue();
}

ElaDxgiScreen::ElaDxgiScreen(QWidget* parent)
    : QWidget(parent)
{
    _pBorderRadius = 5;
    _dxgiManager = ElaDxgiManager::getInstance();
    setFixedSize(700, 500);
    connect(_dxgiManager, &ElaDxgiManager::sendUpdate, this, [=] {
        if (isVisible())
        {
            update();
        }
    });
}

ElaDxgiScreen::~ElaDxgiScreen()
{
}

void ElaDxgiScreen::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.save();
    painter.setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect(), _pBorderRadius, _pBorderRadius);
    painter.drawImage(rect(), _dxgiManager->grabScreenToImage());
    painter.restore();
}

void ElaDxgiScreen::setIsSyncGrabSize(bool isSyncGrabSize)
{
    if (isSyncGrabSize)
    {
        setFixedSize(_dxgiManager->getGrabArea().size());
    }
}

bool ElaDxgiScreen::getIsSyncGrabSize() const
{
    return _isSyncGrabSize;
}
