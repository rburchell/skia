/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "SkPDFDevice.h"
#include "SkPDFDocument.h"
#include "SkPDFPage.h"
#include "SkStream.h"

// Add the resouces, starting at firstIndex to the catalog, removing any dupes.
// A hash table would be really nice here.
void addResoucesToCatalog(int firstIndex, bool firstPage,
                          SkTDArray<SkPDFObject*>* resouceList,
                          SkPDFCatalog* catalog) {
    for (int i = firstIndex; i < resouceList->count(); i++) {
        int index = resouceList->find((*resouceList)[i]);
        if (index != i) {
            // The resouce lists themselves should already be unique, so the
            // first page resouces shouldn't have any dups (assuming the first
            // page resouces are handled first).
            SkASSERT(!firstPage);
            (*resouceList)[i]->unref();
            resouceList->removeShuffle(i);
        } else {
            catalog->addObject((*resouceList)[i], firstPage);
        }
    }
}

SkPDFDocument::SkPDFDocument() : fXRefFileOffset(0) {
    fDocCatalog = new SkPDFDict("Catalog");
    fDocCatalog->unref();  // SkRefPtr and new both took a reference.
    fCatalog.addObject(fDocCatalog.get(), true);
}

SkPDFDocument::~SkPDFDocument() {
    fPages.safeUnrefAll();

    // The page tree has both child and parent pointers, so it creates a
    // reference cycle.  We must clear that cycle to properly reclaim memory.
    for (int i = 0; i < fPageTree.count(); i++)
        fPageTree[i]->clear();
    fPageTree.safeUnrefAll();
    fPageResources.safeUnrefAll();
}

bool SkPDFDocument::emitPDF(SkWStream* stream) {
    if (fPages.isEmpty())
        return false;

    // We haven't emitted the document before if fPageTree is empty.
    if (fPageTree.count() == 0) {
        SkPDFDict* pageTreeRoot;
        SkPDFPage::generatePageTree(fPages, &fCatalog, &fPageTree,
                                    &pageTreeRoot);
        SkRefPtr<SkPDFObjRef> pageTreeRootRef = new SkPDFObjRef(pageTreeRoot);
        pageTreeRootRef->unref();  // SkRefPtr and new both took a reference.
        fDocCatalog->insert("Pages", pageTreeRootRef.get());
        bool first_page = true;
        for (int i = 0; i < fPages.count(); i++) {
            int resourceCount = fPageResources.count();
            fPages[i]->finalizePage(&fCatalog, first_page, &fPageResources);
            addResoucesToCatalog(resourceCount, first_page, &fPageResources,
                                 &fCatalog);
            if (i == 0) {
                first_page = false;
                fSecondPageFirstResouceIndex = fPageResources.count();
            }
        }

        // Figure out the size of things and inform the catalog of file offsets.
        off_t fileOffset = headerSize();
        fileOffset += fCatalog.setFileOffset(fDocCatalog.get(), fileOffset);
        fileOffset += fCatalog.setFileOffset(fPages[0], fileOffset);
        fileOffset += fPages[0]->getPageSize(&fCatalog, fileOffset);
        for (int i = 0; i < fSecondPageFirstResouceIndex; i++)
            fileOffset += fCatalog.setFileOffset(fPageResources[i], fileOffset);
        if (fPages.count() > 1) {
            // TODO(vandebo) For linearized format, save the start of the
            // first page xref table and calculate the size.
        }

        for (int i = 0; i < fPageTree.count(); i++)
            fileOffset += fCatalog.setFileOffset(fPageTree[i], fileOffset);

        for (int i = 1; i < fPages.count(); i++)
            fileOffset += fPages[i]->getPageSize(&fCatalog, fileOffset);

        for (int i = fSecondPageFirstResouceIndex;
                 i < fPageResources.count();
                 i++)
            fileOffset += fCatalog.setFileOffset(fPageResources[i], fileOffset);

        fXRefFileOffset = fileOffset;
    }

    emitHeader(stream);
    fDocCatalog->emitObject(stream, &fCatalog, true);
    fPages[0]->emitObject(stream, &fCatalog, true);
    fPages[0]->emitPage(stream, &fCatalog);
    for (int i = 0; i < fSecondPageFirstResouceIndex; i++)
        fPageResources[i]->emitObject(stream, &fCatalog, true);
    // TODO(vandebo) support linearized format
    //if (fPages.size() > 1) {
    //    // TODO(vandebo) save the file offset for the first page xref table.
    //    fCatalog.emitXrefTable(stream, true);
    //}

    for (int i = 0; i < fPageTree.count(); i++)
        fPageTree[i]->emitObject(stream, &fCatalog, true);

    for (int i = 1; i < fPages.count(); i++)
        fPages[i]->emitPage(stream, &fCatalog);

    for (int i = fSecondPageFirstResouceIndex; i < fPageResources.count(); i++)
        fPageResources[i]->emitObject(stream, &fCatalog, true);

    int64_t objCount = fCatalog.emitXrefTable(stream, fPages.count() > 1);
    emitFooter(stream, objCount);
    return true;
}

bool SkPDFDocument::appendPage(const SkRefPtr<SkPDFDevice>& pdfDevice) {
    if (fPageTree.count() != 0)
        return false;

    SkPDFPage* page = new SkPDFPage(pdfDevice);
    fPages.push(page);  // Reference from new passed to fPages.
    // The rest of the pages will be added to the catalog along with the rest
    // of the page tree.  But the first page has to be marked as such, so we
    // handle it here.
    if (fPages.count() == 1)
        fCatalog.addObject(page, true);
    return true;
}

void SkPDFDocument::emitHeader(SkWStream* stream) {
    stream->writeText("%PDF-1.4\n%");
    // The PDF spec recommends including a comment with four bytes, all
    // with their high bits set.  This is "Skia" with the high bits set.
    stream->write32(0xD3EBE9E1);
    stream->writeText("\n");
}

size_t SkPDFDocument::headerSize() {
    SkDynamicMemoryWStream buffer;
    emitHeader(&buffer);
    return buffer.getOffset();
}

void SkPDFDocument::emitFooter(SkWStream* stream, int64_t objCount) {
    if (fTrailerDict.get() == NULL) {
        fTrailerDict = new SkPDFDict();
        fTrailerDict->unref();  // SkRefPtr and new both took a reference.

        SkPDFInt* objCountInt = new SkPDFInt(objCount);
        fTrailerDict->insert("Size", objCountInt);
        objCountInt->unref();  // insert took a ref and we're done with it.

        // TODO(vandebo) Linearized format will take a Prev entry too.

        SkPDFObjRef* docCatalogRef = new SkPDFObjRef(fDocCatalog.get());
        fTrailerDict->insert("Root", docCatalogRef);
        docCatalogRef->unref();  // insert took a ref and we're done with it.

        // TODO(vandebo) PDF/A requires an ID entry.
    }

    stream->writeText("trailer\n");
    fTrailerDict->emitObject(stream, &fCatalog, false);
    stream->writeText("\nstartxref\n");
    stream->writeBigDecAsText(fXRefFileOffset);
    stream->writeText("\n%%EOF");
}
