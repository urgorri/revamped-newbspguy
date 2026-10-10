#include "lang.h"
#include "VertexBuffer.h"
#include "log.h"
#include "Renderer.h"
#include <cstdint>

VertexBuffer::VertexBuffer(ShaderProgram* shaderProgram, void* dat, int _numVerts, int primitive, bool takeOwnership)
	: frameId(-1),
	  uploaded(false),
	  vboId(0),
	  vaoId(0),
	  data((unsigned char*)dat),
	  numVerts(_numVerts),
	  ownData(takeOwnership),
	  shaderProgram(shaderProgram),
	  primitive(primitive)
{
}

VertexBuffer::VertexBuffer(ShaderProgram* shaderProgram, int primitive)
	: frameId(-1),
	  uploaded(false),
	  vboId(0),
	  vaoId(0),
	  data(nullptr),
	  numVerts(0),
	  ownData(false),
	  shaderProgram(shaderProgram),
	  primitive(primitive)
{
}

VertexBuffer::~VertexBuffer()
{
	if (vboId != 0)
	{
		glDeleteBuffers(1, &vboId);
		vboId = 0;
	}
	if (vaoId != 0)
	{
		glDeleteVertexArrays(1, &vaoId);
		vaoId = 0;
	}
	if (ownData && data)
	{
		delete[] data;
		data = nullptr;
	}
}

void VertexBuffer::setData(void* _data, int _numVerts, bool takeOwnership)
{
	if (ownData && data)
	{
		delete[] data;
	}
	data = static_cast<unsigned char*>(_data);
	numVerts = _numVerts;
	ownData = takeOwnership;
	uploaded = false;
}

unsigned char* VertexBuffer::getData()
{
	if (data == nullptr)
	{
		if (vboId == 0)
		{
			return nullptr;
		}
		glBindBuffer(GL_ARRAY_BUFFER, vboId);
		GLint bufferSize = 0;
		glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &bufferSize);
		if (bufferSize <= 0)
		{
			return nullptr;
		}
		data = new unsigned char[bufferSize];
		ownData = true;
		if (data)
			glGetBufferSubData(GL_ARRAY_BUFFER, 0, bufferSize, data);
	}
	return data;
}

void VertexBuffer::upload(bool hideErrors, bool forceReupload)
{
	if (!shaderProgram)
	{
		if (!hideErrors)
			print_log("VertexBuffer::upload called without shaderProgram");
		return;
	}

	if (uploaded && !forceReupload)
		return;

	if (vaoId == 0)
	{
		glGenVertexArrays(1, &vaoId);
	}
	glBindVertexArray(vaoId);

	if (vboId == 0)
	{
		glGenBuffers(1, &vboId);
	}
	glBindBuffer(GL_ARRAY_BUFFER, vboId);

	if (data == nullptr)
	{
		getData();
	}

	if (data != nullptr && numVerts > 0 && shaderProgram->elementSize > 0)
	{
		GLsizeiptr totalSize = static_cast<GLsizeiptr>(shaderProgram->elementSize) * numVerts;
		if (vboId != 0 && totalSize == lastBufferSize)
		{
			glBufferSubData(GL_ARRAY_BUFFER, 0, totalSize, data);
		}
		else
		{
			glBufferData(GL_ARRAY_BUFFER, totalSize, data, GL_STATIC_DRAW);
			lastBufferSize = totalSize;
		}
	}

	GLintptr offset = 0;
	for (const VertexAttr& a : shaderProgram->attribs)
	{
		if (a.handle == -1)
			continue;

		glEnableVertexAttribArray(a.handle);
		glVertexAttribPointer(a.handle,
							  a.numValues,
							  a.valueType,
							  a.normalized != 0,
							  shaderProgram->elementSize,
							  (const void*)(uintptr_t)offset);
		if (!hideErrors)
		{
			GLenum err = glGetError();
			if (err != GL_NO_ERROR)
			{
				print_log("glVertexAttribPointer error for %s", a.varName);
			}
		}
		offset += a.size;
	}

	if (offset > shaderProgram->elementSize)
	{
		print_log("Vertex attributes exceed elementSize");
	}

	if (ownData)
	{
		delete[] data;
		data = nullptr;
	}
	uploaded = true;
}

void VertexBuffer::reupload()
{
	uploaded = false;
}

void VertexBuffer::drawRange(int _primitive, int start, int end, bool hideErrors)
{
	if (frameId != -1)
	{
		if (frameId == g_drawFrameId)
		{
			if (g_settings.verboseLogs || !hideErrors)
				print_log("Duplicate draw called!\n");
			return;
		}
		frameId = g_drawFrameId;
	}

	if (!shaderProgram)
	{
		if (g_settings.verboseLogs || !hideErrors)
			print_log("No shader program bound for VertexBuffer::drawRange");
		return;
	}

	shaderProgram->bind();
	upload(true, false);

	if (numVerts == 0 || start < 0 || start >= numVerts)
	{
		if (g_settings.verboseLogs || !hideErrors)
			print_log(get_localized_string(LANG_0976), start, numVerts);
		return;
	}
	if (end < 0 || end > numVerts)
	{
		if (g_settings.verboseLogs || !hideErrors)
			print_log(get_localized_string(LANG_0977), end);
		return;
	}
	if (end - start <= 0)
	{
		if (g_settings.verboseLogs || !hideErrors)
			print_log(get_localized_string(LANG_0978), start, end);
		return;
	}

	glBindVertexArray(vaoId);
	glDrawArrays(_primitive, start, end - start);
}

void VertexBuffer::draw(int _primitive)
{
	if (numVerts > 0)
	{
		drawRange(_primitive, 0, numVerts);
	}
}

void VertexBuffer::drawFull()
{
	if (numVerts > 0)
	{
		drawRange(primitive, 0, numVerts);
	}
}
