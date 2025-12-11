#include "pch.h"

#include "SceneHierarchyPanel.h"
#include "Engine/Scene/Components.h"
#include "Engine/Renderer/Renderer.h"
//#include "Engine/Scripting/ScriptEngine.h"
//#include "Engine/UI/UI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <glm/gtc/type_ptr.hpp>

#include <cstring>

/* The Microsoft C++ compiler is non-compliant with the C++ standard and needs
 * the following definition to disable a security warning on std::strncpy().
 */
#ifdef _MSVC_LANG
#define _CRT_SECURE_NO_WARNINGS
#endif

namespace Engine {

	SceneHierarchyPanel::SceneHierarchyPanel(std::shared_ptr<Scene> scene)
	{
		SetContext(scene);
	}

	void SceneHierarchyPanel::SetContext(std::shared_ptr<Scene> scene)
	{
		m_Context = scene;
		m_SelectionContext = {};
	}

	void SceneHierarchyPanel::OnImGuiRender()
	{
		ImGui::Begin("Scene Hierarchy");

		if (m_Context)
		{
			for (auto entityID : m_Context->m_Registry.storage<entt::entity>())
			{
				Entity entity{ entityID, m_Context.get() };
				DrawEntityNode(entity);
			}
			if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
				m_SelectionContext = {};

			// Right-click on blank space
			if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
			{
				if (ImGui::MenuItem("Create Empty Entity"))
					m_Context->CreateEntity("Empty Entity");
				if (ImGui::MenuItem("Create Box Entity"))
				{
					 Entity boxEntity = m_Context->CreateEntity("Box Entity");
					 auto shader = Renderer::GetShaderLibrary()->Get("Mesh.vert.spv");
					 std::shared_ptr material = std::make_shared<Material>(shader);

					 boxEntity.AddComponent<MeshFilterComponent>(std::make_shared<Mesh>(Mesh::CreateCube()));
					 boxEntity.AddComponent<MeshRendererComponent>(material);

				}

				ImGui::EndPopup();
			}

		}
		ImGui::End();

		ImGui::Begin("Properties");
		if (m_SelectionContext)
		{
			DrawComponents(m_SelectionContext);
		}

		ImGui::End();
	}

	void SceneHierarchyPanel::SetSelectedEntity(Entity entity)
	{
		m_SelectionContext = entity;
	}

	void SceneHierarchyPanel::DrawEntityNode(Entity entity)
	{
		auto& tag = entity.GetComponent<TagComponent>().Tag;

		ImGuiTreeNodeFlags flags = ((m_SelectionContext == entity) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
		flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
		bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, flags, tag.c_str());
		if (ImGui::IsItemClicked())
		{
			m_SelectionContext = entity;
		}

		bool entityDeleted = false;
		if (ImGui::BeginPopupContextItem())
		{
			if (ImGui::MenuItem("Delete Entity"))
				entityDeleted = true;

			ImGui::EndPopup();
		}

		if (opened)
		{
			ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
			bool opened = ImGui::TreeNodeEx((void*)9817239, flags, tag.c_str());
			if (opened)
				ImGui::TreePop();
			ImGui::TreePop();
		}

		if (entityDeleted)
		{
			m_Context->DestroyEntity(entity);
			if (m_SelectionContext == entity)
				m_SelectionContext = {};
		}
	}

	static void DrawVec3Control(const std::string& label, glm::vec3& values, float resetValue = 0.0f, float columnWidth = 100.0f)
	{
		ImGuiIO& io = ImGui::GetIO();
		auto boldFont = io.Fonts->Fonts[0];

		ImGui::PushID(label.c_str());

		ImGui::Columns(2);
		ImGui::SetColumnWidth(0, columnWidth);
		ImGui::Text(label.c_str());
		ImGui::NextColumn();

		ImGui::PushMultiItemsWidths(3, ImGui::CalcItemWidth());
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ 0, 0 });

		float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
		ImVec2 buttonSize = { lineHeight + 3.0f, lineHeight };

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.9f, 0.2f, 0.2f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.8f, 0.1f, 0.15f, 1.0f });
		ImGui::PushFont(boldFont);
		if (ImGui::Button("X", buttonSize))
			values.x = resetValue;
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGui::DragFloat("##X", &values.x, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::PopItemWidth();
		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.3f, 0.8f, 0.3f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.2f, 0.7f, 0.2f, 1.0f });
		ImGui::PushFont(boldFont);
		if (ImGui::Button("Y", buttonSize))
			values.y = resetValue;
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGui::DragFloat("##Y", &values.y, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::PopItemWidth();
		ImGui::SameLine();

		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ 0.2f, 0.35f, 0.9f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ 0.1f, 0.25f, 0.8f, 1.0f });
		ImGui::PushFont(boldFont);
		if (ImGui::Button("Z", buttonSize))
			values.z = resetValue;
		ImGui::PopFont();
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		ImGui::DragFloat("##Z", &values.z, 0.1f, 0.0f, 0.0f, "%.2f");
		ImGui::PopItemWidth();

		ImGui::PopStyleVar();

		ImGui::Columns(1);

		ImGui::PopID();
	}

	template<typename T, typename UIFunction>
	static void DrawComponent(const std::string& name, Entity entity, UIFunction uiFunction)
	{
		const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_FramePadding;
		if (entity.HasComponent<T>())
		{
			auto& component = entity.GetComponent<T>();
			ImVec2 contentRegionAvailable = ImGui::GetContentRegionAvail();

			ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{ 4, 4 });
			float lineHeight = ImGui::GetFontSize() + ImGui::GetStyle().FramePadding.y * 2.0f;
			ImGui::Separator();
			bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, name.c_str());
			ImGui::PopStyleVar(
			);
			ImGui::SameLine(contentRegionAvailable.x - lineHeight * 0.5f);
			if (ImGui::Button("+", ImVec2{ lineHeight, lineHeight }))
			{
				ImGui::OpenPopup("ComponentSettings");
			}

			bool removeComponent = false;
			if (ImGui::BeginPopup("ComponentSettings"))
			{
				if (ImGui::MenuItem("Remove component"))
					removeComponent = true;

				ImGui::EndPopup();
			}

			if (open)
			{
				uiFunction(component);
				ImGui::TreePop();
			}

			if (removeComponent)
				entity.RemoveComponent<T>();
		}
	}

	void SceneHierarchyPanel::DrawComponents(Entity entity)
	{
		if (entity.HasComponent<TagComponent>())
		{
			auto& tag = entity.GetComponent<TagComponent>().Tag;

			char buffer[256];
			memset(buffer, 0, sizeof(buffer));
			strncpy_s(buffer, sizeof(buffer), tag.c_str(), sizeof(buffer));
			if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
			{
				tag = std::string(buffer);
			}
		}

		ImGui::SameLine();
		ImGui::PushItemWidth(-1);

		if (ImGui::Button("Add Component"))
			ImGui::OpenPopup("AddComponent");

		if (ImGui::BeginPopup("AddComponent"))
		{
			DisplayAddComponentEntry<CameraComponent>("Camera");
			//DisplayAddComponentEntry<ScriptComponent>("Script");
			DisplayAddComponentEntry<MeshRendererComponent>("Mesh Renderer");
			DisplayAddComponentEntry<MeshFilterComponent>("Mesh Filter");
			//DisplayAddComponentEntry<Rigidbody2DComponent>("Rigidbody 2D");
			//DisplayAddComponentEntry<BoxCollider2DComponent>("Box Collider 2D");
			//DisplayAddComponentEntry<CircleCollider2DComponent>("Circle Collider 2D");
			//DisplayAddComponentEntry<TextComponent>("Text Component");

			ImGui::EndPopup();
		}

		ImGui::PopItemWidth();

		DrawComponent<TransformComponent>("Transform", entity, [](auto& component)
			{
				DrawVec3Control("Translation", component.Translation);
				glm::vec3 rotation = glm::degrees(component.Rotation);
				DrawVec3Control("Rotation", rotation);
				component.Rotation = glm::radians(rotation);
				DrawVec3Control("Scale", component.Scale, 1.0f);
			});

		DrawComponent<CameraComponent>("Camera", entity, [](auto& component)
			{
				auto& camera = component.Camera;

				ImGui::Checkbox("Primary", &component.Primary);

				const char* projectionTypeStrings[] = { "Perspective", "Orthographic" };
				const char* currentProjectionTypeString = projectionTypeStrings[(int)camera.GetProjectionType()];
				if (ImGui::BeginCombo("Projection", currentProjectionTypeString))
				{
					for (int i = 0; i < 2; i++)
					{
						bool isSelected = currentProjectionTypeString == projectionTypeStrings[i];
						if (ImGui::Selectable(projectionTypeStrings[i], isSelected))
						{
							currentProjectionTypeString = projectionTypeStrings[i];
							camera.SetProjectionType((SceneCamera::ProjectionType)i);
						}

						if (isSelected)
							ImGui::SetItemDefaultFocus();
					}

					ImGui::EndCombo();
				}

				if (camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
				{
					float perspectiveVerticalFov = glm::degrees(camera.GetPerspectiveVerticalFOV());
					if (ImGui::DragFloat("Vertical FOV", &perspectiveVerticalFov))
						camera.SetPerspectiveVerticalFOV(glm::radians(perspectiveVerticalFov));

					float perspectiveNear = camera.GetPerspectiveNearClip();
					if (ImGui::DragFloat("Near", &perspectiveNear))
						camera.SetPerspectiveNearClip(perspectiveNear);

					float perspectiveFar = camera.GetPerspectiveFarClip();
					if (ImGui::DragFloat("Far", &perspectiveFar))
						camera.SetPerspectiveFarClip(perspectiveFar);
				}

				if (camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic)
				{
					EG_CORE_ERROR("Can't support orthographic camera!");
				}
			});

		//DrawComponent<ScriptComponent>("Script", entity, [entity, scene = m_Context](auto& component) mutable
		//	{
		//		bool scriptClassExists = ScriptEngine::EntityClassExists(component.ClassName);

		//		static char buffer[64];
		//		strcpy_s(buffer, sizeof(buffer), component.ClassName.c_str());

		//		UI::ScopedStyleColor textColor(ImGuiCol_Text, ImVec4(0.9f, 0.2f, 0.3f, 1.0f), !scriptClassExists);

		//		if (ImGui::InputText("Class", buffer, sizeof(buffer)))
		//		{
		//			component.ClassName = buffer;
		//			return;
		//		}

		//		// Fields
		//		bool sceneRunning = scene->IsRunning();
		//		if (sceneRunning)
		//		{
		//			Ref<ScriptInstance> scriptInstance = ScriptEngine::GetEntityScriptInstance(entity.GetUUID());
		//			if (scriptInstance)
		//			{
		//				const auto& fields = scriptInstance->GetScriptClass()->GetFields();
		//				for (const auto& [name, field] : fields)
		//				{
		//					if (field.Type == ScriptFieldType::Float)
		//					{
		//						float data = scriptInstance->GetFieldValue<float>(name);
		//						if (ImGui::DragFloat(name.c_str(), &data))
		//						{
		//							scriptInstance->SetFieldValue(name, data);
		//						}
		//					}
		//				}
		//			}
		//		}
		//		else
		//		{
		//			if (scriptClassExists)
		//			{
		//				Ref<ScriptClass> entityClass = ScriptEngine::GetEntityClass(component.ClassName);
		//				const auto& fields = entityClass->GetFields();

		//				auto& entityFields = ScriptEngine::GetScriptFieldMap(entity);
		//				for (const auto& [name, field] : fields)
		//				{
		//					// Field has been set in editor
		//					if (entityFields.find(name) != entityFields.end())
		//					{
		//						ScriptFieldInstance& scriptField = entityFields.at(name);

		//						// Display control to set it maybe
		//						if (field.Type == ScriptFieldType::Float)
		//						{
		//							float data = scriptField.GetValue<float>();
		//							if (ImGui::DragFloat(name.c_str(), &data))
		//								scriptField.SetValue(data);
		//						}
		//					}
		//					else
		//					{
		//						// Display control to set it maybe
		//						if (field.Type == ScriptFieldType::Float)
		//						{
		//							float data = 0.0f;
		//							if (ImGui::DragFloat(name.c_str(), &data))
		//							{
		//								ScriptFieldInstance& fieldInstance = entityFields[name];
		//								fieldInstance.Field = field;
		//								fieldInstance.SetValue(data);
		//							}
		//						}
		//					}
		//				}
		//			}
		//		}
		//	});
		DrawComponent<MeshFilterComponent>("Mesh Filter", entity, [](auto& component)
			{
				if (component.Mesh)
				{
					// 展示网格信息（假设你以后给 Mesh 类加了 GetName 方法，现在可以用占位符）
					ImGui::Text("Mesh Data: %p", component.Mesh.get());

					// 如果想展示 Submesh 数量
					ImGui::Text("Submesh Count: %d", (int)component.Mesh->GetSubmeshes().size());
				}
				else
				{
					ImGui::TextDisabled("No mesh assigned.");
				}
			});
		DrawComponent<MeshRendererComponent>("Mesh Renderer", entity, [](auto& component)
			{
				ImGui::Checkbox("Cast Shadows", &component.CastShadows);
				ImGui::Checkbox("Receive Shadows", &component.ReceiveShadows);

				ImGui::Separator();

				if (ImGui::TreeNodeEx("Materials", ImGuiTreeNodeFlags_DefaultOpen))
				{
					auto& materials = component.Materials;
					for (size_t i = 0; i < materials.size(); i++)
					{
						std::string label = "Material " + std::to_string(i);

						// 这里可以点击展开每个材质的具体颜色设置
						if (ImGui::TreeNodeEx((void*)(uint64_t)i, ImGuiTreeNodeFlags_OpenOnArrow, label.c_str()))
						{
							// 这里调用之前实现的 Material::SetColor 逻辑进行简单的颜色编辑
							// 假设你的 Material 类有 GetColor() 方法
							// glm::vec4 color = materials[i]->GetColor();
							// if (ImGui::ColorEdit4("Base Color", glm::value_ptr(color)))
							//     materials[i]->SetColor(color);

							ImGui::Text("Material Instance: %p", materials[i].get());
							ImGui::TreePop();
						}
					}
					ImGui::TreePop();
				}
			});
	}

	template<typename T>
	void SceneHierarchyPanel::DisplayAddComponentEntry(const std::string& entryName) {
		if (!m_SelectionContext.HasComponent<T>())
		{
			if (ImGui::MenuItem(entryName.c_str()))
			{
				m_SelectionContext.AddComponent<T>();
				ImGui::CloseCurrentPopup();
			}
		}
	}

}