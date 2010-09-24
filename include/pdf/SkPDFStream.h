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

#ifndef SkPDFStream_DEFINED
#define SkPDFStream_DEFINED

#include "SkPDFTypes.h"
#include "SkRefCnt.h"
#include "SkTemplates.h"

class SkStream;
class SkPDFCatalog;

/** \class SkPDFStream

    A stream object in a PDF.
*/
// TODO(vandebo) This should handle filters as well.
class SkPDFStream : public SkPDFObject {
public:
    /** Create a PDF stream. A Length entry is automatically added to the
     *  stream dictionary.
     *  @param stream The data part of the stream.
     */
    explicit SkPDFStream(SkStream* stream);
    virtual ~SkPDFStream();

    // The SkPDFObject interface.
    virtual void emitObject(SkWStream* stream, SkPDFCatalog* catalog,
                            bool indirect);
    virtual size_t getOutputSize(SkPDFCatalog* catalog, bool indirect);

    /** Add the value to the stream dictionary with the given key.
     *  @param index The index into the array to set.
     *  @param value The value to add to the array.
     */
    void insert(SkPDFName* key, SkPDFObject* value);

private:
    SkPDFDict fDict;
    SkRefPtr<SkStream> fData;
};

#endif
