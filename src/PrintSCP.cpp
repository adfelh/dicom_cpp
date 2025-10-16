#include "PrintSCP.h"
#include <chrono>
#include <thread>
#include <dcmtk/dcmdata/dctk.h>
#include <dcmtk/dcmimgle/dcmimage.h>

PrintSCP::PrintSCP() : currentAssociation_(NULL) {
    std::cout << "🔄 تهيئة Print SCP..." << std::endl;
}

PrintSCP::~PrintSCP() {
    std::cout << "🧹 تنظيف Print SCP..." << std::endl;
}

OFCondition PrintSCP::handleAssociation(T_ASC_Association* assoc) {
    currentAssociation_ = assoc;
    OFCondition cond = EC_Normal;
    
    T_DIMSE_Message msg;
    T_ASC_PresentationContextID presID;
    
    while (cond.good()) {
        cond = DIMSE_receiveCommand(assoc, DIMSE_NONBLOCKING, 30, &presID, &msg, NULL);
        
        if (cond.good()) {
            switch (msg.CommandField) {
                case DIMSE_N_CREATE_RQ:
                    std::cout << "🖨️ استلام طلب N-CREATE" << std::endl;
                    cond = handleNCreateRequest(msg.msg.NCreateRQ, presID);
                    break;
                case DIMSE_N_ACTION_RQ:
                    std::cout << "⚡ استلام طلب N-ACTION" << std::endl;
                    cond = handleNActionRequest(msg.msg.NActionRQ, presID);
                    break;
                case DIMSE_N_DELETE_RQ:
                    std::cout << "🗑️ استلام طلب N-DELETE" << std::endl;
                    cond = handleNDeleteRequest(msg.msg.NDeleteRQ, presID);
                    break;
                default:
                    std::cout << "❌ أمر DIMSE غير معروف: " << msg.CommandField << std::endl;
                    break;
            }
        } else if (cond == DIMSE_NODATAAVAILABLE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            cond = EC_Normal;
        }
    }
    
    return cond;
}

OFCondition PrintSCP::handleNCreateRequest(const T_DIMSE_N_CreateRQ& req,
                                          T_ASC_PresentationContextID presID) {
    std::cout << "📋 SOP Class: " << req.AffectedSOPClassUID << std::endl;
    std::cout << "🔑 SOP Instance: " << req.AffectedSOPInstanceUID << std::endl;
    
    OFCondition cond = EC_Normal;
    
    if (strcmp(req.AffectedSOPClassUID, UID_BasicFilmSessionSOPClass) == 0) {
        cond = handleFilmSessionCreate();
    } else if (strcmp(req.AffectedSOPClassUID, UID_BasicFilmBoxSOPClass) == 0) {
        cond = handleFilmBoxCreate();
    } else if (strcmp(req.AffectedSOPClassUID, UID_PrinterSOPClass) == 0) {
        cond = handlePrinterCreate();
    } else {
        std::cout << "❌ SOP Class غير معروف: " << req.AffectedSOPClassUID << std::endl;
        cond = EC_IllegalParameter;
    }
    
    Uint16 status = cond.good() ? STATUS_Success : STATUS_N_NoSuchAttribute;
    return sendNCreateResponse(req, presID, status);
}

OFCondition PrintSCP::handleNActionRequest(const T_DIMSE_N_ActionRQ& req,
                                          T_ASC_PresentationContextID presID) {
    std::cout << "⚡ معالجة N-ACTION للنوع: " << req.ActionTypeID << std::endl;
    std::cout << "📋 SOP Class: " << req.RequestedSOPClassUID << std::endl;
    std::cout << "🔑 SOP Instance: " << req.RequestedSOPInstanceUID << std::endl;

    // -------------------
    // 1️⃣ معالجة الصورة الفعلية
    // -------------------
    DcmDataset* dataset = nullptr;
    OFCondition datasetStatus = DIMSE_extractDataset(currentAssociation_, presID, &dataset);
    if (!dataset || datasetStatus.bad()) {
        std::cerr << "❌ لم يتم العثور على Dataset للطباعة" << std::endl;
        return EC_IllegalParameter;
    }

    // استخدام DicomImage من DCMTK
    DicomImage dcmImage(dataset, EXS_Unknown);
    if (dcmImage.getStatus() != EIS_Normal) {
        std::cerr << "❌ خطأ في قراءة DICOM Image" << std::endl;
        return EC_CorruptedData;
    }

    dcmImage.setMinMaxWindow(); // يطبق Windowing تلقائياً

    // إنشاء buffer 8-bit
    const unsigned long width = dcmImage.getWidth();
    const unsigned long height = dcmImage.getHeight();
    std::vector<Uint8> outputBuffer(width * height);

    if (dcmImage.isMonochrome()) {
        for (unsigned long y = 0; y < height; ++y) {
            for (unsigned long x = 0; x < width; ++x) {
                int pixel = dcmImage.getPixel(x, y);
                outputBuffer[y * width + x] = static_cast<Uint8>(pixel);
            }
        }

        // عكس MONOCHROME1 إذا لزم
        if (dcmImage.getPhotometricInterpretation() == EPI_Monochrome1) {
            for (auto& val : outputBuffer)
                val = 255 - val;
        }
    }

    // -------------------
    // 2️⃣ إرسال buffer للطابعة
    // استبدل هذا السطر بدالتك الفعلية للطابعة
    // sendToPrinter(outputBuffer.data(), width, height);
    std::cout << "🖨️ تم تجهيز الصورة للطباعة (" << width << "x" << height << ")" << std::endl;

    // -------------------
    // 3️⃣ إرسال رد نجاح
    T_DIMSE_Message response;
    memset(&response, 0, sizeof(response));
    response.CommandField = DIMSE_N_ACTION_RSP;
    response.msg.NActionRSP.MessageIDBeingRespondedTo = req.MessageID;
    response.msg.NActionRSP.ActionTypeID = req.ActionTypeID;
    response.msg.NActionRSP.DimseStatus = STATUS_Success;
    response.msg.NActionRSP.DataSetType = DIMSE_DATASET_NULL;

    return DIMSE_sendMessageUsingMemoryData(currentAssociation_, presID, 
                                            &response, NULL, NULL, NULL, NULL);
}

OFCondition PrintSCP::handleNDeleteRequest(const T_DIMSE_N_DeleteRQ& req,
                                          T_ASC_PresentationContextID presID) {
    std::cout << "🗑️ معالجة N-DELETE" << std::endl;
    std::cout << "📋 SOP Class: " << req.RequestedSOPClassUID << std::endl;
    std::cout << "🔑 SOP Instance: " << req.RequestedSOPInstanceUID << std::endl;
    
    const char* instanceUID = req.RequestedSOPInstanceUID;
    if (instanceUID) {
        std::lock_guard<std::mutex> lock(sessionMutex_);
        printSessions_.erase(instanceUID);
        std::cout << "✅ حذف جلسة: " << instanceUID << std::endl;
    }
    
    T_DIMSE_Message response;
    memset(&response, 0, sizeof(response));
    
    response.CommandField = DIMSE_N_DELETE_RSP;
    response.msg.NDeleteRSP.MessageIDBeingRespondedTo = req.MessageID;
    response.msg.NDeleteRSP.DimseStatus = STATUS_Success;
    response.msg.NDeleteRSP.DataSetType = DIMSE_DATASET_NULL;
    
    return DIMSE_sendMessageUsingMemoryData(currentAssociation_, presID, 
                                          &response, NULL, NULL, NULL, NULL);
}

OFCondition PrintSCP::sendNCreateResponse(const T_DIMSE_N_CreateRQ& req,
                                         T_ASC_PresentationContextID presID,
                                         Uint16 status) {
    if (!currentAssociation_) return EC_IllegalCall;
    
    T_DIMSE_Message response;
    memset(&response, 0, sizeof(response));
    
    response.CommandField = DIMSE_N_CREATE_RSP;
    response.msg.NCreateRSP.MessageIDBeingRespondedTo = req.MessageID;
    response.msg.NCreateRSP.DimseStatus = status;
    response.msg.NCreateRSP.DataSetType = DIMSE_DATASET_NULL;
    
    DcmDataset* rspDataset = new DcmDataset();
    if (rspDataset && status == STATUS_Success) {
        rspDataset->putAndInsertString(DCM_NumberOfCopies, "1");
        rspDataset->putAndInsertString(DCM_PrintPriority, "MED");
        rspDataset->putAndInsertString(DCM_MediumType, "PAPER");
    }
    
    OFCondition sendCond = DIMSE_sendMessageUsingMemoryData(
        currentAssociation_, presID, &response, NULL, rspDataset, NULL, NULL);
    
    delete rspDataset;
    
    if (sendCond.good()) {
        std::cout << "✅ تم إرسال رد N-CREATE بنجاح" << std::endl;
    } else {
        std::cerr << "❌ فشل في إرسال رد N-CREATE: " << sendCond.text() << std::endl;
    }
    
    return sendCond;
}

OFCondition PrintSCP::handleFilmSessionCreate() {
    std::cout << "🎞️ معالجة إنشاء جلسة فيلم" << std::endl;
    
    std::lock_guard<std::mutex> lock(sessionMutex_);
    printSessions_["default_session"] = "ACTIVE";
    std::cout << "✅ إنشاء جلسة طباعة جديدة" << std::endl;
    
    return EC_Normal;
}

OFCondition PrintSCP::handleFilmBoxCreate() {
    std::cout << "📦 معالجة إنشاء صندوق فيلم" << std::endl;
    return EC_Normal;
}

OFCondition PrintSCP::handlePrinterCreate() {
    std::cout << "🖨️ معالجة إنشاء طابعة" << std::endl;
    return EC_Normal;
}
