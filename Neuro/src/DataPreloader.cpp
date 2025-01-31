#include "DataPreloader.h"
#include "Tensors/Tensor.h"
#include "ComputationalGraph/Placeholder.h"
#include "Tools.h"

namespace Neuro
{
    //////////////////////////////////////////////////////////////////////////
    DataPreloader::DataPreloader(const vector<Tensor*>& destination, const vector<ILoader*>& loaders, size_t capacity, bool threadedMode)
        : m_Destination(destination), m_Loaders(loaders), m_ThreadedMode(threadedMode)
    {
        for (size_t i = 0; i < capacity; ++i)
        {
            vector<Tensor>* data = new vector<Tensor>(destination.size());
            for (size_t i = 0; i < destination.size(); ++i)
                (*data)[i].Resize(destination[i]->GetShape());
            m_Pending.push_back(data);
        }

        if (m_ThreadedMode)
            m_PreloaderThread = thread(&DataPreloader::PreloadFunc, this);
    }

    //////////////////////////////////////////////////////////////////////////
    DataPreloader::~DataPreloader()
    {
        m_Stop = true;
        m_PendingCond.notify_all();
        if (m_ThreadedMode)
            m_PreloaderThread.join();

        for (auto& tVec : m_Pending)
            delete tVec;

        for (auto& tVec : m_Available)
            delete tVec;
    }

    //////////////////////////////////////////////////////////////////////////
    void DataPreloader::Load()
    {
        if (!m_ThreadedMode)
            Preload();

        vector<Tensor>* data = nullptr;
        {
            NVTXProfile p("Waiting for available data", 0xFF93FF72);
            unique_lock<mutex> availableLocker(m_AvailableMtx);
            m_AvailableCond.wait(availableLocker, [this]() {return !m_Available.empty(); });

            data = m_Available.front();
            m_Available.pop_front();
        }

        {
            NVTXProfile p("Copying preloaded data to placeholders", 0xFF93FF72);
            // copy data to destination
            for (size_t i = 0; i < m_Destination.size(); ++i)
            {
                m_Destination[i]->ResizeBatch((*data)[i].Batch());
                (*data)[i].CopyTo(*m_Destination[i]);
            }
        }

        {
            NVTXProfile p("Waiting for pending data lock", 0xFF93FF72);
            unique_lock<mutex> pendingLocker(m_PendingMtx);
            m_Pending.push_back(data);
        }
        m_PendingCond.notify_all();
    }

    //////////////////////////////////////////////////////////////////////////
    void DataPreloader::Preload()
    {
        vector<Tensor>* data = nullptr;

        {
            NVTXProfile p("Waiting for pending data", 0xFF93FF72);
            unique_lock<mutex> pendingLocker(m_PendingMtx);
            m_PendingCond.wait(pendingLocker, [this]() {return !m_Pending.empty() || m_Stop; });

            if (m_Stop)
                return;

            data = m_Pending.front();
            m_Pending.pop_front();
        }

        {
            NVTXProfile p("Loading data", 0xFF93FF72);
            // load data
            size_t loadIdx = 0;
            for (size_t i = 0; i < m_Loaders.size(); ++i)
                loadIdx += (*m_Loaders[i])(*data, loadIdx);

            NEURO_ASSERT(loadIdx == data->size(), "Number or loaded items (" << loadIdx << ") doesn't match number of destinations (" << data->size() << ").");
        }

        {
            NVTXProfile p("Waiting for available data lock", 0xFF93FF72);
            unique_lock<mutex> availableLocker(m_AvailableMtx);
            m_Available.push_back(data);
        }
        m_AvailableCond.notify_all();
    }

    //////////////////////////////////////////////////////////////////////////
    void DataPreloader::PreloadFunc()
    {
        while (true)
        {
            if (m_Stop)
                return;

            Preload();
        }
    }

}