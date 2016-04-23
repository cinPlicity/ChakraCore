//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

#include "JsrtPch.h"
#include "JsrtDebuggerObject.h"
#include "JsrtDebugUtils.h"
#include "JsrtDebugManager.h"

JsrtDebuggerObjectBase::JsrtDebuggerObjectBase(JsrtDebuggerObjectType type, JsrtDebuggerObjectsManager* debuggerObjectsManager) :
    type(type),
    debuggerObjectsManager(debuggerObjectsManager)
{
    Assert(debuggerObjectsManager != nullptr);
    this->handle = debuggerObjectsManager->GetNextHandle();
}

JsrtDebuggerObjectBase::~JsrtDebuggerObjectBase()
{
    this->debuggerObjectsManager = nullptr;
}

JsrtDebuggerObjectsManager * JsrtDebuggerObjectBase::GetDebuggerObjectsManager()
{
    return this->debuggerObjectsManager;
}

Js::DynamicObject * JsrtDebuggerObjectBase::GetChildrens(Js::ScriptContext * scriptContext, uint fromCount, uint totalCount)
{
    AssertMsg(false, "Wrong type for GetChildrens");
    return nullptr;
}

Js::DynamicObject * JsrtDebuggerObjectBase::GetChildrens(WeakArenaReference<Js::IDiagObjectModelWalkerBase>* walkerRef, Js::ScriptContext * scriptContext, uint fromCount, uint totalCount)
{
    Js::DynamicObject* childrensObject = scriptContext->GetLibrary()->CreateObject();

    uint propertiesArrayCount = 0;
    Js::JavascriptArray* propertiesArray = scriptContext->GetLibrary()->CreateArray();

    uint debuggerOnlyPropertiesArrayCount = 0;
    Js::JavascriptArray* debuggerOnlyPropertiesArray = scriptContext->GetLibrary()->CreateArray();

    Js::IDiagObjectModelWalkerBase* walker = walkerRef->GetStrongReference();

    ulong childrensCount = 0;

    if (walker != nullptr)
    {
        try
        {
            childrensCount = walker->GetChildrenCount();
        }
        catch (Js::JavascriptExceptionObject*) {}

        if (fromCount < childrensCount)
        {
            for (ulong i = fromCount; i < childrensCount && (propertiesArrayCount + debuggerOnlyPropertiesArrayCount) < totalCount; ++i)
            {
                Js::ResolvedObject resolvedObject;

                try
                {
                    walker->Get(i, &resolvedObject);
                }
                catch (Js::JavascriptExceptionObject* exception)
                {
                    Js::Var error = exception->GetThrownObject(scriptContext);
                    resolvedObject.obj = error;
                    resolvedObject.address = nullptr;
                    resolvedObject.scriptContext = exception->GetScriptContext();
                    resolvedObject.typeId = Js::JavascriptOperators::GetTypeId(error);
                    resolvedObject.name = _u("{error}");
                    resolvedObject.propId = Js::Constants::NoProperty;
                }

                AutoPtr<WeakArenaReference<Js::IDiagObjectModelDisplay>> objectDisplayWeakRef = resolvedObject.GetObjectDisplay();
                Js::IDiagObjectModelDisplay* resolvedObjectDisplay = objectDisplayWeakRef->GetStrongReference();
                if (resolvedObjectDisplay != nullptr)
                {
                    JsrtDebuggerObjectBase* debuggerObject = JsrtDebuggerObjectProperty::Make(this->GetDebuggerObjectsManager(), objectDisplayWeakRef);
                    Js::DynamicObject* object = debuggerObject->GetJSONObject(resolvedObject.scriptContext);
                    Js::Var marshaledObj = Js::CrossSite::MarshalVar(scriptContext, object);
                    if (resolvedObjectDisplay->IsFake())
                    {
                        Js::JavascriptOperators::OP_SetElementI((Js::Var)debuggerOnlyPropertiesArray, Js::JavascriptNumber::ToVar(debuggerOnlyPropertiesArrayCount++, scriptContext), marshaledObj, scriptContext);
                    }
                    else
                    {
                        Js::JavascriptOperators::OP_SetElementI((Js::Var)propertiesArray, Js::JavascriptNumber::ToVar(propertiesArrayCount++, scriptContext), marshaledObj, scriptContext);
                    }
                    objectDisplayWeakRef->ReleaseStrongReference();
                    objectDisplayWeakRef.Detach();
                }
            }
        }

        walkerRef->ReleaseStrongReference();
    }

    JsrtDebugUtils::AddPropertyToObject(childrensObject, JsrtDebugPropertyId::totalPropertiesOfObject, childrensCount, scriptContext);
    JsrtDebugUtils::AddPropertyToObject(childrensObject, JsrtDebugPropertyId::properties, propertiesArray, scriptContext);
    JsrtDebugUtils::AddPropertyToObject(childrensObject, JsrtDebugPropertyId::debuggerOnlyProperties, debuggerOnlyPropertiesArray, scriptContext);

    return childrensObject;
}

JsrtDebuggerObjectsManager::JsrtDebuggerObjectsManager(JsrtDebugManager* jsrtDebugManager) :
    handleId(0),
    jsrtDebugManager(jsrtDebugManager),
    handleToDebuggerObjectsDictionary(nullptr),
    dataToDebuggerObjectsDictionary(nullptr)
{
    Assert(jsrtDebugManager != nullptr);
}

JsrtDebuggerObjectsManager::~JsrtDebuggerObjectsManager()
{
    if (this->dataToDebuggerObjectsDictionary != nullptr)
    {
        AssertMsg(this->dataToDebuggerObjectsDictionary->Count() == 0, "Should have cleared all debugger objects by now?");

        Adelete(this->GetDebugObjectArena(), this->dataToDebuggerObjectsDictionary);
        this->dataToDebuggerObjectsDictionary = nullptr;
    }

    if (this->handleToDebuggerObjectsDictionary != nullptr)
    {
        AssertMsg(this->handleToDebuggerObjectsDictionary->Count() == 0, "Should have cleared all handle by now?");

        Adelete(this->GetDebugObjectArena(), this->handleToDebuggerObjectsDictionary);
        this->handleToDebuggerObjectsDictionary = nullptr;
    }
}

void JsrtDebuggerObjectsManager::ClearAll()
{
    if (this->dataToDebuggerObjectsDictionary != nullptr)
    {
        this->dataToDebuggerObjectsDictionary->Clear();
    }

    if (this->handleToDebuggerObjectsDictionary != nullptr)
    {
        this->handleToDebuggerObjectsDictionary->Map([this](uint handle, JsrtDebuggerObjectBase* debuggerObject) {
            Adelete(this->GetDebugObjectArena(), debuggerObject);
        });
        this->handleToDebuggerObjectsDictionary->Clear();
    }

    this->handleId = 0;
}

ArenaAllocator * JsrtDebuggerObjectsManager::GetDebugObjectArena()
{
    return this->GetJsrtDebugManager()->GetDebugObjectArena();
}

bool JsrtDebuggerObjectsManager::TryGetDebuggerObjectFromHandle(uint handle, JsrtDebuggerObjectBase ** debuggerObject)
{
    if (this->handleToDebuggerObjectsDictionary == nullptr)
    {
        return false;
    }

    return this->handleToDebuggerObjectsDictionary->TryGetValue(handle, debuggerObject);
}

void JsrtDebuggerObjectsManager::AddToDebuggerObjectsDictionary(JsrtDebuggerObjectBase * debuggerObject)
{
    Assert(debuggerObject != nullptr);

    uint handle = debuggerObject->GetHandle();

    Assert(handle > 0);

    if (this->handleToDebuggerObjectsDictionary == nullptr)
    {
        this->handleToDebuggerObjectsDictionary = Anew(this->GetDebugObjectArena(), DebuggerObjectsDictionary, this->GetDebugObjectArena(), 10);
    }

    Assert(!this->handleToDebuggerObjectsDictionary->ContainsKey(handle));

    int index = this->handleToDebuggerObjectsDictionary->Add(handle, debuggerObject);

    Assert(index != -1);
}

void JsrtDebuggerObjectsManager::AddToDataToDebuggerObjectsDictionary(void * data, JsrtDebuggerObjectBase * debuggerObject)
{
    Assert(data != nullptr);
    Assert(debuggerObject != nullptr);

    if (this->dataToDebuggerObjectsDictionary == nullptr)
    {
        this->dataToDebuggerObjectsDictionary = Anew(this->GetDebugObjectArena(), DataToDebuggerObjectsDictionary, this->GetDebugObjectArena(), 10);
    }

    Assert(!this->dataToDebuggerObjectsDictionary->ContainsKey(data));

    int index = this->dataToDebuggerObjectsDictionary->Add(data, debuggerObject);

    Assert(index != -1);

    this->AddToDebuggerObjectsDictionary(debuggerObject);
}

bool JsrtDebuggerObjectsManager::TryGetDataFromDataToDebuggerObjectsDictionary(void * data, JsrtDebuggerObjectBase ** debuggerObject)
{
    if (this->dataToDebuggerObjectsDictionary == nullptr)
    {
        return false;
    }

    return this->dataToDebuggerObjectsDictionary->TryGetValue(data, debuggerObject);
}

JsrtDebuggerStackFrame::JsrtDebuggerStackFrame(JsrtDebuggerObjectsManager * debuggerObjectsManager, Js::DiagStackFrame * stackFrame, uint frameIndex) :
    debuggerObjectsManager(debuggerObjectsManager),
    frameIndex(frameIndex),
    stackFrame(stackFrame),
    pObjectModelWalker(nullptr)
{
    Assert(this->stackFrame != nullptr);
}

JsrtDebuggerStackFrame::~JsrtDebuggerStackFrame()
{
    this->stackFrame = nullptr;

    if (this->pObjectModelWalker != nullptr)
    {
        HeapDelete(this->pObjectModelWalker);
        this->pObjectModelWalker = nullptr;
    }
}

Js::DynamicObject * JsrtDebuggerStackFrame::GetJSONObject(Js::ScriptContext* scriptContext)
{
    Js::ScriptContext *frameScriptContext = stackFrame->GetScriptContext();
    Js::DynamicObject* stackTraceObject = frameScriptContext->GetLibrary()->CreateObject();

    Js::FunctionBody* functionBody = stackFrame->GetFunction();
    Js::Utf8SourceInfo* utf8SourceInfo = functionBody->GetUtf8SourceInfo();

    JsrtDebugUtils::AddPropertyToObject(stackTraceObject, JsrtDebugPropertyId::index, frameIndex, scriptContext);
    JsrtDebugUtils::AddScriptIdToObject(stackTraceObject, utf8SourceInfo);

    int currentByteCodeOffset = stackFrame->GetByteCodeOffset();

    if (stackFrame->IsInterpreterFrame() && frameIndex != 0)
    {
        // For non-leaf interpreter frames back up 1 instruction so we see the caller
        // rather than the statement after the caller
        currentByteCodeOffset--;
    }

    JsrtDebugUtils::AddLineColumnToObject(stackTraceObject, functionBody, currentByteCodeOffset);
    JsrtDebugUtils::AddSourceLengthAndTextToObject(stackTraceObject, functionBody, currentByteCodeOffset);

    JsrtDebuggerObjectBase* functionObject = JsrtDebuggerObjectFunction::Make(this->debuggerObjectsManager, functionBody);
    JsrtDebugUtils::AddPropertyToObject(stackTraceObject, JsrtDebugPropertyId::functionHandle, functionObject->GetHandle(), frameScriptContext);

    return stackTraceObject;
}

Js::DynamicObject * JsrtDebuggerStackFrame::GetLocalsObject()
{
    Js::ScriptContext* scriptContext = this->stackFrame->GetScriptContext();

    /*
        {
            "exception" : {},
            "arguments" : {},
            "returnValue" : {},
            "functionCallsReturn" : [{}, {}],
            "locals" : [],
            "scopes" : [{}, {}],
            "globals" : {}
        }
     */

    Js::DynamicObject* propertiesObject = scriptContext->GetLibrary()->CreateObject();

    Js::Var returnValueObject = nullptr;

    uint functionCallsReturnCount = 0;
    Js::JavascriptArray* functionCallsReturn = scriptContext->GetLibrary()->CreateArray();

    uint totalLocalsCount = 0;
    Js::JavascriptArray* localsArray = scriptContext->GetLibrary()->CreateArray();

    uint scopesCount = 0;
    Js::JavascriptArray* scopesArray = scriptContext->GetLibrary()->CreateArray();

    Js::DynamicObject* globalsObject = nullptr;

    if (this->pObjectModelWalker != nullptr)
    {
        HeapDelete(this->pObjectModelWalker);
    }

    ReferencedArenaAdapter* pRefArena = scriptContext->GetThreadContext()->GetDebugManager()->GetDiagnosticArena();
    Js::IDiagObjectModelDisplay* pLocalsDisplay = Anew(pRefArena->Arena(), Js::LocalsDisplay, this->stackFrame);
    this->pObjectModelWalker = pLocalsDisplay->CreateWalker();

    if (this->pObjectModelWalker != nullptr)
    {
        Js::LocalsWalker* localsWalker = (Js::LocalsWalker*)this->pObjectModelWalker->GetStrongReference();

        if (localsWalker != nullptr)
        {
            ulong totalProperties = localsWalker->GetChildrenCount();
            if (totalProperties > 0)
            {
                int index = 0;
                Js::ResolvedObject resolvedObject;
                resolvedObject.scriptContext = this->stackFrame->GetScriptContext();

                if (Js::VariableWalkerBase::GetExceptionObject(index, this->stackFrame, &resolvedObject))
                {
                    JsrtDebuggerObjectBase::CreateDebuggerObject<JsrtDebuggerObjectProperty>(this->debuggerObjectsManager, resolvedObject, scriptContext, [&](Js::Var marshaledObj)
                    {
                        JsrtDebugUtils::AddPropertyToObject(propertiesObject, JsrtDebugPropertyId::exception, marshaledObj, scriptContext);
                    });
                }

                if (localsWalker->HasUserNotDefinedArguments() && localsWalker->CreateArgumentsObject(&resolvedObject))
                {
                    JsrtDebuggerObjectBase::CreateDebuggerObject<JsrtDebuggerObjectProperty>(this->debuggerObjectsManager, resolvedObject, scriptContext, [&](Js::Var marshaledObj)
                    {
                        JsrtDebugUtils::AddPropertyToObject(propertiesObject, JsrtDebugPropertyId::arguments, marshaledObj, scriptContext);
                    });
                }

                Js::ReturnedValueList *returnedValueList = this->stackFrame->GetScriptContext()->GetDebugContext()->GetProbeContainer()->GetReturnedValueList();

                if (returnedValueList != nullptr && returnedValueList->Count() > 0 && this->stackFrame->IsTopFrame())
                {
                    for (int i = 0; i < returnedValueList->Count(); ++i)
                    {
                        Js::ReturnedValue * returnValue = returnedValueList->Item(i);
                        Js::VariableWalkerBase::GetReturnedValueResolvedObject(returnValue, this->stackFrame, &resolvedObject);

                        JsrtDebuggerObjectBase::CreateDebuggerObject<JsrtDebuggerObjectProperty>(debuggerObjectsManager, resolvedObject, scriptContext, [&](Js::Var marshaledObj)
                        {

                            if (returnValue->isValueOfReturnStatement)
                            {
                                returnValueObject = marshaledObj;
                            }
                            else
                            {
                                Js::JavascriptOperators::OP_SetElementI((Js::Var)functionCallsReturn, Js::JavascriptNumber::ToVar(functionCallsReturnCount, scriptContext), marshaledObj, scriptContext);
                                functionCallsReturnCount++;
                            }
                        });
                    }

                    if (returnValueObject != nullptr)
                    {
                        JsrtDebugUtils::AddPropertyToObject(propertiesObject, JsrtDebugPropertyId::returnValue, returnValueObject, scriptContext);
                    }

                    if (functionCallsReturnCount > 0)
                    {
                        JsrtDebugUtils::AddPropertyToObject(propertiesObject, JsrtDebugPropertyId::functionCallsReturn, functionCallsReturn, scriptContext);
                    }
                }

                ulong localsCount = localsWalker->GetLocalVariablesCount();
                for (ulong i = 0; i < localsCount; ++i)
                {
                    if (!localsWalker->GetLocal(i, &resolvedObject))
                    {
                        break;
                    }

                    JsrtDebuggerObjectBase::CreateDebuggerObject<JsrtDebuggerObjectProperty>(debuggerObjectsManager, resolvedObject, scriptContext, [&](Js::Var marshaledObj)
                    {
                        Js::JavascriptOperators::OP_SetElementI((Js::Var)localsArray, Js::JavascriptNumber::ToVar(totalLocalsCount, scriptContext), marshaledObj, scriptContext);
                        totalLocalsCount++;
                    });
                }


                index = 0;
                BOOL foundGroup = TRUE;
                while (foundGroup)
                {
                    foundGroup = localsWalker->GetScopeObject(index++, &resolvedObject);
                    if (foundGroup == TRUE)
                    {
                        AutoPtr<WeakArenaReference<Js::IDiagObjectModelDisplay>> objectDisplayWeakRef = resolvedObject.GetObjectDisplay();
                        JsrtDebuggerObjectBase* debuggerObject = JsrtDebuggerObjectScope::Make(debuggerObjectsManager, objectDisplayWeakRef, scopesCount);
                        Js::DynamicObject* object = debuggerObject->GetJSONObject(resolvedObject.scriptContext);
                        Assert(object != nullptr);
                        Js::Var marshaledObj = Js::CrossSite::MarshalVar(scriptContext, object);
                        Js::JavascriptOperators::OP_SetElementI((Js::Var)scopesArray, Js::JavascriptNumber::ToVar(scopesCount, scriptContext), marshaledObj, scriptContext);
                        scopesCount++;
                        objectDisplayWeakRef.Detach();
                    }
                }

                if (localsWalker->GetGlobalsObject(&resolvedObject))
                {
                    JsrtDebuggerObjectBase::CreateDebuggerObject<JsrtDebuggerObjectGlobalsNode>(this->debuggerObjectsManager, resolvedObject, scriptContext, [&](Js::Var marshaledObj)
                    {
                        globalsObject = (Js::DynamicObject*)marshaledObj;
                    });
                }
            }

            this->pObjectModelWalker->ReleaseStrongReference();
        }

        Adelete(pRefArena->Arena(), pLocalsDisplay);
    }

    JsrtDebugUtils::AddPropertyToObject(propertiesObject, JsrtDebugPropertyId::locals, localsArray, scriptContext);
    JsrtDebugUtils::AddPropertyToObject(propertiesObject, JsrtDebugPropertyId::scopes, scopesArray, scriptContext);

    if (globalsObject == nullptr)
    {
        globalsObject = scriptContext->GetLibrary()->CreateObject();
    }

    JsrtDebugUtils::AddPropertyToObject(propertiesObject, JsrtDebugPropertyId::globals, globalsObject, scriptContext);

    return propertiesObject;
}

Js::DynamicObject* JsrtDebuggerStackFrame::Evaluate(const char16 * pszSrc, bool isLibraryCode)
{
    Js::DynamicObject* evalResult = nullptr;
    if (this->stackFrame != nullptr)
    {
        Js::ResolvedObject resolvedObject;
        HRESULT hr = S_OK;
        Js::ScriptContext* scriptContext = this->stackFrame->GetScriptContext();
        Js::JavascriptExceptionObject *exceptionObject = nullptr;
        {
            BEGIN_JS_RUNTIME_CALL_EX_AND_TRANSLATE_EXCEPTION_AND_ERROROBJECT_TO_HRESULT_NESTED(scriptContext, false)
            {
                this->stackFrame->EvaluateImmediate(pszSrc, isLibraryCode, &resolvedObject);
            }
            END_JS_RUNTIME_CALL_AND_TRANSLATE_AND_GET_EXCEPTION_AND_ERROROBJECT_TO_HRESULT(hr, scriptContext, exceptionObject);
        }
        if (resolvedObject.obj == nullptr)
        {
            resolvedObject.name = _u("{exception}");
            resolvedObject.typeId = Js::TypeIds_Error;
            resolvedObject.address = nullptr;

            if (exceptionObject != nullptr)
            {
                resolvedObject.obj = exceptionObject->GetThrownObject(scriptContext);
            }
            else
            {
                resolvedObject.obj = scriptContext->GetLibrary()->GetUndefined();
            }
        }
        if (resolvedObject.obj != nullptr)
        {
            resolvedObject.scriptContext = scriptContext;

            charcount_t len = Js::JavascriptString::GetBufferLength(pszSrc);
            resolvedObject.name = AnewNoThrowArray(this->debuggerObjectsManager->GetDebugObjectArena(), WCHAR, len + 1);
            if (resolvedObject.name == nullptr)
            {
                return nullptr;
            }
            wcscpy_s((WCHAR*)resolvedObject.name, len + 1, pszSrc);

            resolvedObject.typeId = Js::JavascriptOperators::GetTypeId(resolvedObject.obj);
            JsrtDebuggerObjectBase::CreateDebuggerObject<JsrtDebuggerObjectProperty>(this->debuggerObjectsManager, resolvedObject, this->stackFrame->GetScriptContext(), [&](Js::Var marshaledObj)
            {
                evalResult = (Js::DynamicObject*)marshaledObj;
            });
        }
    }
    return evalResult;
}

JsrtDebuggerObjectProperty::JsrtDebuggerObjectProperty(JsrtDebuggerObjectsManager* debuggerObjectsManager, WeakArenaReference<Js::IDiagObjectModelDisplay>* objectDisplay) :
    JsrtDebuggerObjectBase(JsrtDebuggerObjectType::Property, debuggerObjectsManager),
    objectDisplay(objectDisplay),
    walkerRef(nullptr)
{
    Assert(objectDisplay != nullptr);
}

JsrtDebuggerObjectProperty::~JsrtDebuggerObjectProperty()
{
    if (this->objectDisplay != nullptr)
    {
        HeapDelete(this->objectDisplay);
        this->objectDisplay = nullptr;
    }

    if (this->walkerRef != nullptr)
    {
        HeapDelete(this->walkerRef);
        this->walkerRef = nullptr;
    }
}

JsrtDebuggerObjectBase * JsrtDebuggerObjectProperty::Make(JsrtDebuggerObjectsManager* debuggerObjectsManager, WeakArenaReference<Js::IDiagObjectModelDisplay>* objectDisplay)
{
    JsrtDebuggerObjectBase* debuggerObject = Anew(debuggerObjectsManager->GetDebugObjectArena(), JsrtDebuggerObjectProperty, debuggerObjectsManager, objectDisplay);

    debuggerObjectsManager->AddToDebuggerObjectsDictionary(debuggerObject);

    return debuggerObject;
}

Js::DynamicObject * JsrtDebuggerObjectProperty::GetJSONObject(Js::ScriptContext* scriptContext)
{
    Js::IDiagObjectModelDisplay* objectDisplayRef = this->objectDisplay->GetStrongReference();

    Js::DynamicObject* propertyObject = nullptr;

    if (objectDisplayRef != nullptr)
    {
        propertyObject = scriptContext->GetLibrary()->CreateObject();

        JsrtDebugUtils::AddPropertyToObject(propertyObject, JsrtDebugPropertyId::name, objectDisplayRef->Name(), scriptContext);

        JsrtDebugUtils::AddPropertyType(propertyObject, objectDisplayRef, scriptContext); // Will add type, value, display, className, propertyAttributes

        JsrtDebugUtils::AddPropertyToObject(propertyObject, JsrtDebugPropertyId::handle, this->GetHandle(), scriptContext);

        this->objectDisplay->ReleaseStrongReference();
    }

    return propertyObject;
}

Js::DynamicObject* JsrtDebuggerObjectProperty::GetChildrens(Js::ScriptContext* scriptContext, uint fromCount, uint totalCount)
{
    Js::IDiagObjectModelDisplay* objectDisplayRef = objectDisplay->GetStrongReference();
    if (objectDisplayRef == nullptr)
    {
        return nullptr;
    }

    if (this->walkerRef == nullptr)
    {
        this->walkerRef = objectDisplayRef->CreateWalker();
    }

    Js::DynamicObject* childrens = __super::GetChildrens(this->walkerRef, scriptContext, fromCount, totalCount);

    objectDisplay->ReleaseStrongReference();

    return childrens;
}

JsrtDebuggerObjectScope::JsrtDebuggerObjectScope(JsrtDebuggerObjectsManager * debuggerObjectsManager, WeakArenaReference<Js::IDiagObjectModelDisplay>* objectDisplay, uint index) :
    JsrtDebuggerObjectBase(JsrtDebuggerObjectType::Scope, debuggerObjectsManager),
    objectDisplay(objectDisplay),
    index(index),
    walkerRef(nullptr)
{
    Assert(this->objectDisplay != nullptr);
}

JsrtDebuggerObjectScope::~JsrtDebuggerObjectScope()
{
    if (this->objectDisplay != nullptr)
    {
        HeapDelete(this->objectDisplay);
        this->objectDisplay = nullptr;
    }

    if (this->walkerRef != nullptr)
    {
        HeapDelete(this->walkerRef);
        this->walkerRef = nullptr;
    }
}

JsrtDebuggerObjectBase * JsrtDebuggerObjectScope::Make(JsrtDebuggerObjectsManager * debuggerObjectsManager, WeakArenaReference<Js::IDiagObjectModelDisplay>* objectDisplay, uint index)
{
    JsrtDebuggerObjectBase* debuggerObject = Anew(debuggerObjectsManager->GetDebugObjectArena(), JsrtDebuggerObjectScope, debuggerObjectsManager, objectDisplay, index);

    debuggerObjectsManager->AddToDebuggerObjectsDictionary(debuggerObject);

    return debuggerObject;
}

Js::DynamicObject * JsrtDebuggerObjectScope::GetJSONObject(Js::ScriptContext* scriptContext)
{
    Js::IDiagObjectModelDisplay* modelDisplay = this->objectDisplay->GetStrongReference();

    Js::DynamicObject* scopeObject = nullptr;

    if (modelDisplay != nullptr)
    {
        scopeObject = scriptContext->GetLibrary()->CreateObject();
        JsrtDebugUtils::AddPropertyToObject(scopeObject, JsrtDebugPropertyId::index, this->index, scriptContext);
        JsrtDebugUtils::AddPropertyToObject(scopeObject, JsrtDebugPropertyId::handle, this->GetHandle(), scriptContext);

        this->objectDisplay->ReleaseStrongReference();
    }

    return scopeObject;
}

Js::DynamicObject * JsrtDebuggerObjectScope::GetChildrens(Js::ScriptContext * scriptContext, uint fromCount, uint totalCount)
{
    Js::IDiagObjectModelDisplay* objectDisplayRef = objectDisplay->GetStrongReference();
    if (objectDisplayRef == nullptr)
    {
        return nullptr;
    }

    if (this->walkerRef == nullptr)
    {
        this->walkerRef = objectDisplayRef->CreateWalker();
    }

    Js::DynamicObject* childrens = __super::GetChildrens(this->walkerRef, scriptContext, fromCount, totalCount);

    objectDisplay->ReleaseStrongReference();

    return childrens;
}

JsrtDebuggerObjectFunction::JsrtDebuggerObjectFunction(JsrtDebuggerObjectsManager* debuggerObjectsManager, Js::FunctionBody* functionBody) :
    JsrtDebuggerObjectBase(JsrtDebuggerObjectType::Function, debuggerObjectsManager),
    functionBody(functionBody)
{
}

JsrtDebuggerObjectFunction::~JsrtDebuggerObjectFunction()
{
    this->functionBody = nullptr;
}

JsrtDebuggerObjectBase * JsrtDebuggerObjectFunction::Make(JsrtDebuggerObjectsManager * debuggerObjectsManager, Js::FunctionBody * functionBody)
{
    JsrtDebuggerObjectBase* debuggerObject = nullptr;

    if (debuggerObjectsManager->TryGetDataFromDataToDebuggerObjectsDictionary(functionBody, &debuggerObject))
    {
        Assert(debuggerObject != nullptr);
        return debuggerObject;
    }

    debuggerObject = Anew(debuggerObjectsManager->GetDebugObjectArena(), JsrtDebuggerObjectFunction, debuggerObjectsManager, functionBody);

    debuggerObjectsManager->AddToDataToDebuggerObjectsDictionary(functionBody, debuggerObject);

    return debuggerObject;
}

Js::DynamicObject * JsrtDebuggerObjectFunction::GetJSONObject(Js::ScriptContext * scriptContext)
{
    Js::DynamicObject* functionObject = scriptContext->GetLibrary()->CreateObject();

    JsrtDebugUtils::AddScriptIdToObject(functionObject, this->functionBody->GetUtf8SourceInfo());
    JsrtDebugUtils::AddPropertyToObject(functionObject, JsrtDebugPropertyId::line, this->functionBody->GetLineNumber(), scriptContext);
    JsrtDebugUtils::AddPropertyToObject(functionObject, JsrtDebugPropertyId::column, this->functionBody->GetColumnNumber(), scriptContext);
    JsrtDebugUtils::AddPropertyToObject(functionObject, JsrtDebugPropertyId::name, this->functionBody->GetDisplayName(), scriptContext);
    JsrtDebugUtils::AddPropertyToObject(functionObject, JsrtDebugPropertyId::type, scriptContext->GetLibrary()->GetFunctionTypeDisplayString()->GetSz(), scriptContext);
    JsrtDebugUtils::AddPropertyToObject(functionObject, JsrtDebugPropertyId::handle, this->GetHandle(), scriptContext);

    return functionObject;
}

JsrtDebuggerObjectGlobalsNode::JsrtDebuggerObjectGlobalsNode(JsrtDebuggerObjectsManager* debuggerObjectsManager, WeakArenaReference<Js::IDiagObjectModelDisplay>* objectDisplay) :
    JsrtDebuggerObjectBase(JsrtDebuggerObjectType::Globals, debuggerObjectsManager),
    objectDisplay(objectDisplay),
    walkerRef(nullptr)
{
    Assert(objectDisplay != nullptr);
}

JsrtDebuggerObjectGlobalsNode::~JsrtDebuggerObjectGlobalsNode()
{
    if (this->objectDisplay != nullptr)
    {
        HeapDelete(this->objectDisplay);
        this->objectDisplay = nullptr;
    }

    if (this->walkerRef != nullptr)
    {
        HeapDelete(this->walkerRef);
        this->walkerRef = nullptr;
    }
}

JsrtDebuggerObjectBase * JsrtDebuggerObjectGlobalsNode::Make(JsrtDebuggerObjectsManager * debuggerObjectsManager, WeakArenaReference<Js::IDiagObjectModelDisplay>* objectDisplay)
{
    JsrtDebuggerObjectBase* debuggerObject = Anew(debuggerObjectsManager->GetDebugObjectArena(), JsrtDebuggerObjectGlobalsNode, debuggerObjectsManager, objectDisplay);

    debuggerObjectsManager->AddToDebuggerObjectsDictionary(debuggerObject);

    return debuggerObject;
}

Js::DynamicObject * JsrtDebuggerObjectGlobalsNode::GetJSONObject(Js::ScriptContext * scriptContext)
{
    Js::IDiagObjectModelDisplay* objectDisplayRef = this->objectDisplay->GetStrongReference();

    Js::DynamicObject* globalsNode = nullptr;

    if (objectDisplayRef != nullptr)
    {
        globalsNode = scriptContext->GetLibrary()->CreateObject();
        JsrtDebugUtils::AddPropertyToObject(globalsNode, JsrtDebugPropertyId::handle, this->GetHandle(), scriptContext);
        this->objectDisplay->ReleaseStrongReference();
    }

    return globalsNode;
}

Js::DynamicObject * JsrtDebuggerObjectGlobalsNode::GetChildrens(Js::ScriptContext * scriptContext, uint fromCount, uint totalCount)
{
    Js::IDiagObjectModelDisplay* objectDisplayRef = objectDisplay->GetStrongReference();
    if (objectDisplayRef == nullptr)
    {
        return nullptr;
    }

    if (this->walkerRef == nullptr)
    {
        this->walkerRef = objectDisplayRef->CreateWalker();
    }

    Js::DynamicObject* childrens = __super::GetChildrens(this->walkerRef, scriptContext, fromCount, totalCount);

    objectDisplay->ReleaseStrongReference();

    return childrens;
}

JsrtDebugStackFrames::JsrtDebugStackFrames(JsrtDebugManager* jsrtDebugManager):
    framesDictionary(nullptr)
{
    Assert(jsrtDebugManager != nullptr);
    this->jsrtDebugManager = jsrtDebugManager;
}

JsrtDebugStackFrames::~JsrtDebugStackFrames()
{
    if (this->framesDictionary != nullptr)
    {
        this->framesDictionary->Map([this](uint handle, JsrtDebuggerStackFrame* debuggerStackFrame) {
            Adelete(this->jsrtDebugManager->GetDebugObjectArena(), debuggerStackFrame);
        });
        this->framesDictionary->Clear();
        this->framesDictionary = nullptr;
    }
}

Js::JavascriptArray * JsrtDebugStackFrames::StackFrames(Js::ScriptContext * scriptContext)
{
    Js::JavascriptArray* stackTraceArray = scriptContext->GetLibrary()->CreateArray();

    if (this->framesDictionary == nullptr)
    {
        this->framesDictionary = Anew(this->jsrtDebugManager->GetDebugObjectArena(), FramesDictionary, this->jsrtDebugManager->GetDebugObjectArena(), 10);
    }
    else
    {
        this->framesDictionary->Clear();
    }

    uint frameCount = 0;

    for (Js::ScriptContext *tempScriptContext = scriptContext->GetThreadContext()->GetScriptContextList();
    tempScriptContext != nullptr && tempScriptContext->IsScriptContextInDebugMode();
        tempScriptContext = tempScriptContext->next)
    {
        Js::WeakDiagStack * framePointers = tempScriptContext->GetDebugContext()->GetProbeContainer()->GetFramePointers();
        if (framePointers != nullptr)
        {
            Js::DiagStack* stackFrames = framePointers->GetStrongReference();
            if (stackFrames != nullptr)
            {
                int count = stackFrames->Count();
                for (int frameIndex = 0; frameIndex < count; ++frameIndex)
                {
                    Js::DiagStackFrame* stackFrame = stackFrames->Peek(frameIndex);
                    Js::DynamicObject* stackTraceObject = this->GetStackFrame(stackFrame, frameCount);

                    Js::Var marshaledObj = Js::CrossSite::MarshalVar(scriptContext, stackTraceObject);
                    Js::JavascriptOperators::OP_SetElementI((Js::Var)stackTraceArray, Js::JavascriptNumber::ToVar(frameCount++, scriptContext), marshaledObj, scriptContext);
                }
            }
            framePointers->ReleaseStrongReference();
            HeapDelete(framePointers);
        }
    }

    return stackTraceArray;
}

bool JsrtDebugStackFrames::TryGetFrameObjectFromFrameIndex(uint frameIndex, JsrtDebuggerStackFrame ** debuggerStackFrame)
{
    if (this->framesDictionary != nullptr)
    {
        return this->framesDictionary->TryGetValue(frameIndex, debuggerStackFrame);
    }

    return false;
}

Js::DynamicObject * JsrtDebugStackFrames::GetStackFrame(Js::DiagStackFrame * stackFrame, uint frameIndex)
{
    JsrtDebuggerStackFrame* debuggerStackFrame = Anew(this->jsrtDebugManager->GetDebugObjectArena(), JsrtDebuggerStackFrame, this->jsrtDebugManager->GetDebuggerObjectsManager(), stackFrame, frameIndex);

    Assert(this->framesDictionary != nullptr);

    this->framesDictionary->Add(frameIndex, debuggerStackFrame);

    return debuggerStackFrame->GetJSONObject(stackFrame->GetScriptContext());
}