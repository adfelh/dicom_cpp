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
    OFCondition status = DIMSE_extractDataset(currentAssociation_, presID, &dataset);
    if (!dataset) {
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
    // (هنا ضع الدالة الخاصة بك لإرسال البكسلات للطابعة)
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

// باقي الدوال (handleNDeleteRequest, sendNCreateResponse, handleFilmSessionCreate, handleFilmBoxCreate, handlePrinterCreate)
// تبقى كما هي في النسخة الأصلية

