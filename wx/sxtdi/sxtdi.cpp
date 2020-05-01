/***************************************************************************\

    Copyright (c) 2001-2020 David Schmenk

    All rights reserved.

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the
    "Software"), to deal in the Software without restriction, including
    without limitation the rights to use, copy, modify, merge, publish,
    distribute, and/or sell copies of the Software, and to permit persons
    to whom the Software is furnished to do so, provided that the above
    copyright notice(s) and this permission notice appear in all copies of
    the Software and that both the above copyright notice(s) and this
    permission notice appear in supporting documentation.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
    OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
    OF THIRD PARTY RIGHTS. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
    HOLDERS INCLUDED IN THIS NOTICE BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL
    INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING
    FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
    NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
    WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

    Except as contained in this notice, the name of a copyright holder
    shall not be used in advertising or otherwise to promote the sale, use
    or other dealings in this Software without prior written authorization
    of the copyright holder.

\***************************************************************************/
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif
#include <wx/numdlg.h>
#include <wx/cmdline.h>
#include "sxtdi.h"
#define ALIGN_EXP       2000
#define MAX_WHITE       65535
#define INC_BLACK       1024
#define MIN_BLACK       0
#define MAX_BLACK       32768
#define PIX_BITWIDTH    16
#define MIN_PIX         0
#define MAX_PIX         ((1<<PIX_BITWIDTH)-1)
#define LUT_BITWIDTH    10
#define LUT_SIZE        (1<<LUT_BITWIDTH)
#define LUT_INDEX(i)    ((i)>>(PIX_BITWIDTH-LUT_BITWIDTH))
#define max(a,b)        ((a)>=(b)?(a):(b))
int ccdModel = SXCCD_MX5;
/*
 * 16 bit image sample to RGB pixel LUT
 */
uint8_t redLUT[LUT_SIZE];
uint8_t blugrnLUT[LUT_SIZE];
static void calcRamp(int black, int white, float gamma, bool filter)
{
    int pix, offset;
    float scale, recipg, pixClamp;

    offset = LUT_INDEX(black) - 1;
    scale  = (float)MAX_PIX/(white - black);
    recipg = 1.0/gamma;
    for (pix = 0; pix < LUT_SIZE; pix++)
    {
        pixClamp = ((float)(pix - offset)/(LUT_SIZE-1)) * scale;
        if (pixClamp > 1.0) pixClamp = 1.0;
        else if (pixClamp < 0.0) pixClamp = 0.0;
        redLUT[pix]    = 255.0 * pow(pixClamp, recipg);
        blugrnLUT[pix] = filter ? 0 : redLUT[pix];
    }
}
/*
 * Star registration
 */
static void calcCentroid(int width, int height, uint16_t *pixels, int x, int y, int x_radius, int y_radius, float *x_centroid, float *y_centroid, int min)
{
    int   i, j;
    float sum;

    *x_centroid = 0.0;
    *y_centroid = 0.0;
    sum         = 0.0;

    for (j = y - y_radius; j <= y + y_radius; j++)
        for (i = x - x_radius; i <= x + x_radius; i++)
            if (pixels[j * width + i] > min)
            {
                *x_centroid += i * pixels[j * width + i];
                *y_centroid += j * pixels[j * width + i];
                sum         +=     pixels[j * width + i];
            }
    if (sum != 0.0)
    {
        *x_centroid /= sum;
        *y_centroid /= sum;
    }
}
static bool findBestCentroid(int width, int height, uint16_t *pixels, float *x_centroid, float *y_centroid, int x_range, int y_range, int *x_max_radius, int *y_max_radius, float sigs)
{
    int   i, j, x, y, x_min, x_max, y_min, y_max, x_radius, y_radius;
    int   pixel, pixel_min, pixel_max;
    float pixel_ave, pixel_sig;

    x           = (int)*x_centroid;
    y           = (int)*y_centroid;
    x_min       = x - x_range;
    x_max       = x + x_range;
    y_min       = y - y_range;
    y_max       = y + y_range;
    x           = -1;
    y           = -1;
    x_radius    = 0;
    y_radius    = 0;
    pixel_ave   = 0.0;
    pixel_sig   = 0.0;
    /*
    if (x_min < 0)      x_min = 0;
    if (x_max > width)  x_max = width;
    if (y_min < 0)      y_min = 0;
    if (y_max > height) y_max = height;
    */
    if (x_min < *x_max_radius)        x_min = *x_max_radius;
    if (x_max > width-*x_max_radius)  x_max = width-*x_max_radius;
    if (y_min < *y_max_radius)        y_min = *y_max_radius;
    if (y_max > height-*y_max_radius) y_max = height-*y_max_radius;

    for (j = y_min; j < y_max; j++)
        for (i = x_min; i < x_max; i++)
            pixel_ave += pixels[j * width + i];
    pixel_ave /= (y_max - y_min) * (x_max - x_min);
    for (j = y_min; j < y_max; j++)
        for (i = x_min; i < x_max; i++)
            pixel_sig += (pixel_ave - pixels[j * width + i]) * (pixel_ave - pixels[j * width + i]);
    pixel_sig = sqrt(pixel_sig / ((y_max - y_min) * (x_max - x_min) - 1));
    pixel_max = pixel_min = pixel_ave + pixel_sig * sigs;
    for (j = y_min; j < y_max; j++)
    {
        for (i = x_min; i < x_max; i++)
        {
            if (pixels[j * width + i] > pixel_max)
            {
                pixel = pixels[j * width + i];
                /*
                 * Local maxima
                 */
                if ((pixel >= pixels[j * width + i + 1])
                 && (pixel >= pixels[j * width + i - 1])
                 && (pixel >= pixels[(j + 1) * width + i])
                 && (pixel >= pixels[(j - 1) * width + i]))
                {
                    /*
                     * Avoid hot pixels
                    if ((pixel_min < ((pixel_type *)pixels)[j * image->width + i + 1])
                     && (pixel_min < ((pixel_type *)pixels)[j * image->width + i - 1])
                     && (pixel_min < ((pixel_type *)pixels)[(j + 1) * image->width + i])
                     && (pixel_min < ((pixel_type *)pixels)[(j - 1) * image->width + i]))
                     */
                    {
                        /*
                         * Find radius of highlight
                         */
                        for (y_radius = 1; (y_radius <= *y_max_radius)
                                        && ((pixels[(j + y_radius) * width + i] > pixel_min)
                                         || (pixels[(j - y_radius) * width + i] > pixel_min)); y_radius++);
                        for (x_radius = 1; (x_radius <= *x_max_radius)
                                        && ((pixels[j * width + i + x_radius] > pixel_min)
                                         || (pixels[j * width + i - x_radius] > pixel_min)); x_radius++);
                        /*
                         * If its really big, skip it.  Something like the moon
                         */
                        if (x_radius < *x_max_radius && y_radius < *y_max_radius)
                        {
                            pixel_max = pixel;
                            calcCentroid(width, height, pixels, i, j, x_radius, y_radius, x_centroid, y_centroid, pixel_min);
                            x = (int)(*x_centroid + 0.5);
                            y = (int)(*y_centroid + 0.5);
                        }
                    }
                }
            }
        }
    }
    if  (x >= 0 && y >= 0)
    {
        *x_max_radius = x_radius;
        *y_max_radius = y_radius;
    }
    printf("Best star @ %d, %d\n", x, y);
    return x >= 0 && y >= 0;
}
class ScanApp : public wxApp
{
public:
    virtual void OnInitCmdLine(wxCmdLineParser &parser);
    virtual bool OnCmdLineParsed(wxCmdLineParser &parser);
    virtual bool OnInit();
};
class ScanFrame : public wxFrame
{
public:
    ScanFrame();
private:
    int          tdiState;
    unsigned int ccdFrameX, ccdFrameY, ccdFrameWidth, ccdFrameHeight, ccdFrameDepth;
    unsigned int ccdPixelWidth, ccdPixelHeight;
    uint16_t    *ccdFrame;
    uint16_t    *tdiFrame;
    float        pixelGamma;
    bool         pixelFilter;
    int          tdiLength, tdiRow, numFrames;
    int          tdiExposure, tdiMinutes;
    float        tdiScanRate;
    float        trackStarInitialX, trackStarInitialY, trackStarX, trackStarY;
    wxImage     *scanImage;
    wxTimer      tdiTimer;
    void DoAlign();
    void DoTDI();
    void OnTimer(wxTimerEvent& event);
    void OnNew(wxCommandEvent& event);
    void OnSave(wxCommandEvent& event);
    void OnExit(wxCommandEvent& event);
    void OnFilter(wxCommandEvent& event);
    void OnAlign(wxCommandEvent& event);
    void OnScan(wxCommandEvent& event);
    void OnDuration(wxCommandEvent& event);
    void OnStop(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnBackground(wxEraseEvent& event);
    void OnPaint(wxPaintEvent& event);
    wxDECLARE_EVENT_TABLE();
};
enum
{
    ID_TIMER = 1,
    ID_FILTER,
    ID_ALIGN,
    ID_SCAN,
    ID_DURATION,
    ID_STOP,
};
enum
{
    STATE_IDLE = 0,
    STATE_ALIGNING,
    STATE_SCANNING,
    STATE_STOPPING
};
wxBEGIN_EVENT_TABLE(ScanFrame, wxFrame)
    EVT_TIMER(ID_TIMER,     ScanFrame::OnTimer)
    EVT_MENU(ID_FILTER,     ScanFrame::OnFilter)
    EVT_MENU(ID_ALIGN,      ScanFrame::OnAlign)
    EVT_MENU(ID_SCAN,       ScanFrame::OnScan)
    EVT_MENU(ID_DURATION,   ScanFrame::OnDuration)
    EVT_MENU(ID_STOP,       ScanFrame::OnStop)
    EVT_MENU(wxID_NEW,      ScanFrame::OnNew)
    EVT_MENU(wxID_SAVE,     ScanFrame::OnSave)
    EVT_MENU(wxID_ABOUT,    ScanFrame::OnAbout)
    EVT_MENU(wxID_EXIT,     ScanFrame::OnExit)
    EVT_ERASE_BACKGROUND(   ScanFrame::OnBackground)
    EVT_PAINT(              ScanFrame::OnPaint)
wxEND_EVENT_TABLE()
wxIMPLEMENT_APP(ScanApp);
void ScanApp::OnInitCmdLine(wxCmdLineParser &parser)
{
    wxApp::OnInitCmdLine(parser);
    parser.AddOption(wxT("m"), wxT("model"), wxT("camera model override"), wxCMD_LINE_VAL_STRING);
}
bool ScanApp::OnCmdLineParsed(wxCmdLineParser &parser)
{
    wxString *modelString = new wxString(' ', 10);
    if (parser.Found(wxT("m"), modelString))
    {
        //
        // Validate ccdModel
        //
        if (toupper(modelString->GetChar(1)) == 'X')
        {
            switch (toupper(modelString->GetChar(0)))
            {
                case 'H':
                    ccdModel &= ~SXCCD_INTERLEAVE;
                    break;
                case 'M':
                    ccdModel |= SXCCD_INTERLEAVE;
                    break;
                default:
                    printf("Invalid model type designation.\n");
            }
            switch ((char)modelString->GetChar(2))
            {
                case '5':
                case '7':
                case '9':
                    ccdModel = (ccdModel & 0xC0) | (modelString->GetChar(2) - '0');
                    break;
                default:
                    printf("Invalid model number designation.\n");
            }
            if (toupper(modelString->GetChar(3)) == 'C')
                ccdModel |= SXCCD_COLOR;
            else
                ccdModel &= ~SXCCD_COLOR;
        }
        else
            printf("Invalid SX designation.\n");
        printf("SX model now: 0x%02X\n", ccdModel);
    }
    return wxApp::OnCmdLineParsed(parser);
}
bool ScanApp::OnInit()
{
    if (wxApp::OnInit())
    {
        ScanFrame *frame = new ScanFrame();
        frame->Show(true);
        return true;
    }
    return false;
}
ScanFrame::ScanFrame() : wxFrame(NULL, wxID_ANY, "SX TDI"), tdiTimer(this, ID_TIMER)
{
    char statusText[40];
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(wxID_NEW, "&New\tCtrl-N");
    menuFile->Append(wxID_SAVE, "&Save...\tCtrl-S");
    menuFile->AppendSeparator();
    menuFile->Append(wxID_EXIT);
    wxMenu *menuScan = new wxMenu;
    menuScan->AppendCheckItem(ID_FILTER, wxT("&Red Filter\tR"));
    menuScan->Append(ID_ALIGN, "&Align\tA");
    menuScan->Append(ID_SCAN, "&TDI Scan\tT");
    menuScan->Append(ID_DURATION, "Scan &Duration\tD");
    menuScan->Append(ID_STOP, "S&top\tCtrl-T");
    wxMenu *menuHelp = new wxMenu;
    menuHelp->Append(wxID_ABOUT);
    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append(menuFile, "&File");
    menuBar->Append(menuScan, "&Scan");
    menuBar->Append(menuHelp, "&Help");
    SetMenuBar(menuBar);
    tdiFrame    = NULL;
    tdiState    = STATE_IDLE;
    tdiMinutes  = 0;
    tdiExposure = 0;
    tdiScanRate = 0.0;
    pixelGamma  = 1.0;
    pixelFilter = false;
    calcRamp(MIN_PIX, MAX_PIX, pixelGamma, pixelFilter);
    if (sxOpen(ccdModel))
    {
        ccdModel = sxGetModel(0);
        sxGetFrameDimensions(0, &ccdFrameWidth, &ccdFrameHeight, &ccdFrameDepth);
        sxGetPixelDimensions(0, &ccdPixelWidth, &ccdPixelHeight);
        ccdFrame = (uint16_t *)malloc(sizeof(uint16_t) * ccdFrameWidth * ccdFrameHeight);
        scanImage = new wxImage(ccdFrameHeight, ccdFrameWidth);
        sprintf(statusText, "Attached: %cX-%d", ccdModel & SXCCD_INTERLEAVE ? 'M' : 'H', ccdModel & 0x3F);
    }
    else
    {
        ccdModel        = 0;
        ccdFrameWidth   = ccdFrameHeight = 512;
        ccdFrameDepth   = 16;
        ccdPixelWidth   = ccdPixelHeight = 1;
        ccdFrame        = NULL;
        scanImage       = NULL;
        strcpy(statusText, "Attached: None");
    }
    int winHeight = ccdFrameWidth; // Swap width/height
    int winWidth  = ccdFrameHeight;
    while (winHeight > 720) // Constrain initial size to something reasonable
    {
        winWidth  <<= 1;
        winHeight <<= 1;
    }
    CreateStatusBar(2);
    SetStatusText(statusText, 0);
    SetStatusText("Rate: -.--- row/s", 1);
    SetClientSize(winWidth, winHeight);
}
void ScanFrame::OnBackground(wxEraseEvent& event)
{
}
void ScanFrame::OnPaint(wxPaintEvent& event)
{
    int winWidth, winHeight;
    GetClientSize(&winWidth, &winHeight);
    if (winWidth > 0 && winHeight > 0)
    {
        wxClientDC dc(this);
        if (scanImage)
        {
            wxBitmap bitmap(scanImage->Scale(winWidth, winHeight, wxIMAGE_QUALITY_BILINEAR));
            dc.DrawBitmap(bitmap, 0, 0);
        }
        else
        {
            dc.SetBrush(wxBrush(*wxWHITE));
            dc.DrawRectangle(0, 0, winWidth, winHeight);
        }
    }
}
void ScanFrame::DoAlign()
{
    int xRadius, yRadius;
    uint16_t ccdInvertFrame[ccdFrameWidth * ccdFrameHeight];
    sxReadPixels(0, // cam idx
                 SXCCD_EXP_FLAGS_FIELD_BOTH, // options
                 0, // xoffset
                 0, // yoffset
                 ccdFrameWidth, // width
                 ccdFrameHeight, // height
                 1, // xbin
                 1, // ybin
                 (unsigned char *)ccdInvertFrame); //pixbuf
    for (int invert = 0; invert < ccdFrameHeight; invert++)
        memcpy(&ccdFrame[invert * ccdFrameWidth], &ccdInvertFrame[(ccdFrameHeight - 1 - invert) * ccdFrameWidth], ccdFrameWidth * sizeof(uint16_t));
    if (numFrames == 0)
    {
        //
        // If first frame, identify best candidate for measuring scan rate
        //
        trackStarInitialX = ccdFrameWidth/2;
        trackStarInitialY = 0.0;
        xRadius = yRadius = 15;
        if (findBestCentroid(ccdFrameWidth, ccdFrameHeight, ccdFrame, &trackStarInitialX, &trackStarInitialY, ccdFrameWidth, ccdFrameHeight - ccdFrameHeight/4, &xRadius, &yRadius, 1.0))
        {
            printf("Start tracking star at %f, %f\n", trackStarInitialX, trackStarInitialY);
            trackStarX        = trackStarInitialX;
            trackStarY        = trackStarInitialY;
            numFrames         = 1;
        }
    }
    else
    {
        //
        // Track star for rate measurement
        //
        xRadius = yRadius = 15;
        if (trackStarY < (ccdFrameHeight - 1 - (tdiScanRate * ALIGN_EXP/1000)))
            if (findBestCentroid(ccdFrameWidth, ccdFrameHeight, ccdFrame, &trackStarX, &trackStarY, 5, ccdFrameHeight, &xRadius, &yRadius, 1.0))
            {
                printf("Tracking star at %f, %f\n", trackStarX, trackStarY);
                if (trackStarInitialY < trackStarY)
                {
                    tdiExposure = (ALIGN_EXP * numFrames) / (trackStarY - trackStarInitialY);
                    tdiScanRate = (trackStarY - trackStarInitialY) / (ALIGN_EXP/1000 * numFrames++);
                }
            }
    }
    int winWidth, winHeight;
    GetClientSize(&winWidth, &winHeight);
    if (winWidth > 0 && winHeight > 0)
    {
        int pixelMax = MIN_PIX;
        int pixelMin = MAX_PIX;
        unsigned char *rgb = scanImage->GetData();
        for (int y = 0; y < ccdFrameWidth; y++) // Rotate image 90 degrees counterclockwise as it gets copied
        {
            uint16_t *m16 = &(ccdFrame[ccdFrameWidth - y - 1]);
            for (int x = 0; x < ccdFrameHeight; x++)
            {
                if (*m16 > pixelMax) pixelMax = *m16;
                if (*m16 < pixelMin) pixelMin = *m16;
                rgb[0] = max(rgb[0], redLUT[LUT_INDEX(*m16)]);
                rgb[1] = max(rgb[1], blugrnLUT[LUT_INDEX(*m16)]);
                rgb[2] = max(rgb[2], blugrnLUT[LUT_INDEX(*m16)]);
                rgb   += 3;
                m16   += ccdFrameWidth;
            }
        }
        calcRamp(pixelMin, pixelMax, pixelGamma, pixelFilter);
        wxClientDC dc(this);
        wxBitmap bitmap(scanImage->Scale(winWidth, winHeight, wxIMAGE_QUALITY_BILINEAR));
        dc.DrawBitmap(bitmap, 0, 0);
        char statusText[40];
        sprintf(statusText, "Rate: %2.3f row/s", tdiScanRate);
        SetStatusText(statusText, 1);
    }
}
void ScanFrame::DoTDI()
{
    uint16_t *ccdRow = &tdiFrame[tdiRow * ccdFrameWidth];
    sxReadPixels(0, // cam idx
                 SXCCD_EXP_FLAGS_TDI_SCAN |
                 SXCCD_EXP_FLAGS_FIELD_BOTH, // options
                 0, // xoffset
                 0, // yoffset
                 ccdFrameWidth, // width
                 1, // height
                 1, // xbin
                 1, // ybin
                 (unsigned char *)ccdRow); //pixbuf
    int winWidth, winHeight;
    GetClientSize(&winWidth, &winHeight);
    if (winWidth > 0 && winHeight > 0)
    {
        int pixelMax       = MIN_PIX;
        int pixelMin       = MAX_PIX;
        unsigned char *rgb = scanImage->GetData();
        uint16_t *pixels   = (tdiRow < ccdFrameHeight) ? tdiFrame : &tdiFrame[ccdFrameWidth * (tdiRow - ccdFrameHeight)];
        for (int y = 0; y < ccdFrameWidth; y++) // Rotate image 90 degrees counterclockwise as it gets copied
        {
            uint16_t *m16 = &pixels[ccdFrameWidth - y - 1];
            for (int x = 0; x < ccdFrameHeight; x++)
            {
                if (*m16 > pixelMax) pixelMax = *m16;
                if (*m16 < pixelMin) pixelMin = *m16;
                rgb[0] = redLUT[LUT_INDEX(*m16)];
                rgb[1] = blugrnLUT[LUT_INDEX(*m16)];
                rgb[2] = blugrnLUT[LUT_INDEX(*m16)];
                rgb   += 3;
                m16   += ccdFrameWidth;
            }
        }
        calcRamp(pixelMin, pixelMax, pixelGamma, pixelFilter);
        wxClientDC dc(this);
        wxBitmap bitmap(scanImage->Scale(winWidth, winHeight, wxIMAGE_QUALITY_BILINEAR));
        dc.DrawBitmap(bitmap, 0, 0);
    }
    if (++tdiRow == tdiLength)
    {
        tdiTimer.Stop();
        tdiState = STATE_IDLE;
    }
}
void ScanFrame::OnTimer(wxTimerEvent& event)
{
    if (tdiState == STATE_SCANNING)
        DoTDI();
    else
        DoAlign();
}
void ScanFrame::OnAlign(wxCommandEvent& event)
{
    if (tdiState == STATE_IDLE)
    {
        if (ccdModel)
        {
            sxClearFrame(0, SXCCD_EXP_FLAGS_FIELD_BOTH);
            tdiTimer.Start(ALIGN_EXP);
            memset(scanImage->GetData(), 0, ccdFrameWidth * ccdFrameHeight * 3);
            for (int y = 0; y < ccdFrameWidth; y += ccdFrameWidth/32)
            {
                unsigned char *rgb = scanImage->GetData() + y * ccdFrameHeight * 3;
                for (int x = 0; x < ccdFrameHeight; x++)
                {
                    rgb[1] = 128;
                    rgb   += 3;
                }
            }
            tdiScanRate = 0.0;
            numFrames   = 0;
            tdiState    = STATE_ALIGNING;
        }
    }
}
void ScanFrame::OnDuration(wxCommandEvent& event)
{
    wxNumberEntryDialog dlg(this,
                            wxT(""),
                            wxT("Hours:"),
                            wxT("Scan Duration"),
                            6, 1, 12);
    if (dlg.ShowModal() != wxID_OK)
        return;
    tdiMinutes = dlg.GetValue() * 60;
}
void ScanFrame::OnScan(wxCommandEvent& event)
{
    if (tdiState == STATE_IDLE)
    {
        if (ccdModel)
        {
            if (tdiFrame != NULL)
            {
                //
                // OVerwrite existing scan?
                //
                return;
            }
            if (tdiExposure == 0)
            {
                wxLogMessage("Align & Measure Rate first");
                return;
            }
            if (tdiMinutes == 0)
            {
                OnDuration(event);
                if (tdiMinutes == 0)
                    return;
            }
            tdiLength = tdiMinutes * 60000 / tdiExposure;
            if (tdiLength < ccdFrameHeight)
                tdiLength = ccdFrameHeight;
            tdiFrame  = (uint16_t *)malloc(sizeof(uint16_t) * tdiLength * ccdFrameWidth);
            memset(tdiFrame, 0, sizeof(uint16_t) * tdiLength * ccdFrameWidth);
            sxClearFrame(0, SXCCD_EXP_FLAGS_FIELD_BOTH);
            tdiTimer.Start(tdiExposure);
            tdiRow   = 0;
            tdiState = STATE_SCANNING;
        }
    }
}
void ScanFrame::OnStop(wxCommandEvent& event)
{
    if (tdiTimer.IsRunning())
    {
        tdiTimer.Stop();
        if (tdiState == STATE_SCANNING)
            tdiLength = tdiRow;
        tdiState = STATE_IDLE;
    }
}
void ScanFrame::OnNew(wxCommandEvent& event)
{
    if (tdiFrame != NULL)
    {
        //
        // Overwrite existing scan?
        //
        free(tdiFrame);
        tdiFrame = NULL;
    }
}
void ScanFrame::OnSave(wxCommandEvent& event)
{
    wxLogMessage("Save Scan");
}
void ScanFrame::OnExit(wxCommandEvent& event)
{
    if (tdiState != STATE_IDLE)
        ;
    if (scanImage != NULL)
        delete scanImage;
    Close(true);

}
void ScanFrame::OnFilter(wxCommandEvent& event)
{
    pixelFilter = event.IsChecked();
}
void ScanFrame::OnAbout(wxCommandEvent& event)
{
    wxMessageBox("Starlight Xpress Time Delay Integration Scanner\nCopyright (c) 2020, David Schmenk", "About SX Focus", wxOK | wxICON_INFORMATION);
}
