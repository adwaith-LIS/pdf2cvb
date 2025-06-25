#include <iostream>
#include <fpdfview.h>
#include <iCVCImg.h>

#include <windows.h>//for relative path


BOOL renderPageToCVB(FPDF_PAGE page, int dpi, int pageIndex, IMG &cvbImg);

int main()
{
    char buffer[MAX_PATH];
    GetCurrentDirectoryA(MAX_PATH, buffer);
    std::string currentPath(buffer);
    currentPath.resize(currentPath.length() - 9+1);

    std::string relative_docPath = currentPath + "\\test_input\\input.pdf";

    FPDF_STRING documentPath = relative_docPath.c_str();
    FPDF_BYTESTRING documentPassword = NULL;
    const int dpi = 300;

    FPDF_LIBRARY_CONFIG cfg{ sizeof(FPDF_LIBRARY_CONFIG), 0, 0, 0, 0 };
    FPDF_InitLibraryWithConfig(&cfg);

    FPDF_DOCUMENT doc = FPDF_LoadDocument(documentPath,documentPassword); // doc path, password
    if (!doc) {
        unsigned long err = FPDF_GetLastError();
        std::cout<< "cannot open PDF\n";
        return 2; 
    }
    else {
        std::cout << "Loaded PDF Successfully\n";
    }

    for (size_t i = 0; i < FPDF_GetPageCount(doc); i++) 
    {
        FPDF_PAGE page = FPDF_LoadPage(doc, i);
        IMG cvbImg = nullptr;
        if(!renderPageToCVB(page, dpi, i,cvbImg))
        {
            std::cout << "failed to convert pdf to CVB image\n";
        }
        if (cvbImg != NULL && IsImage(cvbImg)) {
            std::string relative_outputPath = currentPath + "\\test_input\\output.bmp";
            WriteImageFile(cvbImg, relative_outputPath.c_str());
        }
        ReleaseObject(cvbImg);
        FPDF_ClosePage(page);
    }
    FPDF_CloseDocument(doc);
    FPDF_DestroyLibrary();


    return 0;
}


BOOL renderPageToCVB(FPDF_PAGE page, int dpi, int pageIndex,IMG &cvbImg)
{

    const double pdfW = FPDF_GetPageWidth(page);
    const double pdfH = FPDF_GetPageHeight(page);

    const int width = static_cast<int>(pdfW * dpi / 72.0);
    const int height = static_cast<int>(pdfH * dpi / 72.0);

    // Create 32-bit bitmap (BGRx layout)
    FPDF_BITMAP bmp = FPDFBitmap_CreateEx(width, height, FPDFBitmap_BGRx, nullptr, 0);
    if (!bmp) return false;

    FPDFBitmap_FillRect(bmp, 0, 0, width, height, 0xFFFFFFFF);  // white background
    FPDF_RenderPageBitmap(bmp, page, 0, 0, width, height, 0,
        FPDF_ANNOT | FPDF_LCD_TEXT);

    const uint8_t* pdfBuffer = static_cast<const uint8_t*>(FPDFBitmap_GetBuffer(bmp));
    const int stride = FPDFBitmap_GetStride(bmp);

    // Create CVB image (3 planes, interleaved, 8-bit, flipped memory layout)
    cvbImg = nullptr;
    if (!CreateGenericImage(3, width, height, false, cvbImg) || !cvbImg)
    {
        FPDFBitmap_Destroy(bmp);
        return false;
    }

    // Access each plane using GetImageVPA and write data
    void* base = nullptr;
    PVPAT vpat[3] = { nullptr, nullptr, nullptr };

    for (int p = 0; p < 3; ++p)
    {
        if (!GetImageVPA(cvbImg, p, &base, &vpat[p]))
        {
            std::cerr << "GetImageVPA failed for plane " << p << "\n";
            ReleaseObject(cvbImg);
            FPDFBitmap_Destroy(bmp);
            return false;
        }
    }


    // Copy PDFium buffer into CVB interleaved layout (R = plane 0, G = 1, B = 2)
	// PDFium delivers BGRx, **top-down** (row 0 = top of page).
	// CVB’s image memory is bottom-up, but the VPAT we get from
	// GetImageVPA already maps logical row 0 -> correct memory address,
	// so we copy rows in natural order (no manual Y-flip needed).
    for (int y = 0; y < height; ++y)
    {
        //const uint8_t* srcLine = pdfBuffer + (height - 1 - y) * stride; // flip Y
        const uint8_t* srcLine = pdfBuffer + y * stride; // No flip, VPAT handles it

        for (int x = 0; x < width; ++x)
        {
            const int srcIdx = x * 4;
            const uint8_t b = srcLine[srcIdx + 0];
            const uint8_t g = srcLine[srcIdx + 1];
            const uint8_t r = srcLine[srcIdx + 2];

            for (int p = 0; p < 3; ++p)
            {
                const intptr_t yAddr = reinterpret_cast<intptr_t>(base) + vpat[p][y].YEntry;
                const intptr_t pixAddr = yAddr + vpat[p][x].XEntry;
                uint8_t* dstPixel = reinterpret_cast<uint8_t*>(pixAddr);

                if (p == 0) *dstPixel = r; // plane 0 = R
                else if (p == 1) *dstPixel = g; // plane 1 = G
                else if (p == 2) *dstPixel = b; // plane 2 = B
            }
        }
    }

    FPDFBitmap_Destroy(bmp);

    return true;
}
